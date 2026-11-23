// DhtNode.cpp - the Chord node: server, router and local index store
#include "DhtNode.h"
#include "Hashing.h"

#include <chrono>

using namespace std;

DhtNode::DhtNode() {
    running = false;
    nextFinger = 0;
    messagesSent = 0;
    messagesReceived = 0;
    lookupCount = 0;
    hopTotal = 0;
    lookupMsTotal = 0;
}

DhtNode::~DhtNode() {
    stop();
}

bool DhtNode::start(const Address& dhtAddr, const Address& transfer) {
    transferAddr = transfer;

    // The node id comes from the address, so restarting a peer on the same port
    // gives it the same place on the ring.
    NodeInfo me = makeNode(nodeIdOf(dhtAddr.ip, dhtAddr.port), dhtAddr.ip, dhtAddr.port);
    routing.setSelf(me);

    if (!startServer("0.0.0.0", dhtAddr.port)) return false;
    logLine("[DHT] node id " + to_string(me.id) + " listening on " + dhtAddr.toString());
    return true;
}

void DhtNode::stop() {
    if (running) {
        running = false;
        if (maintenance.joinable()) maintenance.join();
    }
    stopServer();
}

// ===================== joining and leaving the ring =====================

// First peer in the network: it is its own successor, so it owns every key.
void DhtNode::createRing() {
    NodeInfo me = routing.getSelf();
    routing.setSuccessor(me);
    routing.clearPredecessor();
    running = true;
    maintenance = thread(&DhtNode::maintenanceLoop, this);
    logLine("[DHT] created a new ring (I am alone, so I own all " + to_string(ID_SPACE) + " keys)");
}

// Join through a bootstrap peer. The bootstrap is used ONLY to find where I
// belong; after this it is an ordinary neighbour with no special role.
bool DhtNode::joinRing(const Address& bootstrap) {
    NodeInfo me = routing.getSelf();

    // The bootstrap's id is unknown until it answers, so ask it directly.
    NodeInfo entry = makeNode(0, bootstrap.ip, bootstrap.port);
    string reply;
    if (!ask(entry, "PING", reply)) {
        logLine("[DHT] bootstrap peer " + bootstrap.toString() + " is not reachable");
        return false;
    }
    vector<string> w = splitWords(reply);
    long long bootId = 0;
    if (w.size() != 2 || w[0] != "PONG" || !parseInt(w[1], bootId)) {
        logLine("[DHT] bootstrap peer gave a bad reply");
        return false;
    }
    entry.id = (unsigned int)bootId;

    // Ask the ring who should be my successor: the node responsible for my id.
    // The lookup runs over the overlay, starting at the bootstrap peer.
    NodeInfo current = entry;
    NodeInfo successor;
    for (int step = 0; step < MAX_LOOKUP_STEPS; step++) {
        if (!ask(current, "FIND_STEP " + to_string(me.id), reply)) {
            logLine("[DHT] join failed: " + current.toString() + " stopped answering");
            return false;
        }
        vector<string> r = splitWords(reply);
        NodeInfo node;
        if (r.size() < 4 || !parseNode(r, 1, node)) return false;
        if (r[0] == "SUCC") {
            successor = node;
            break;
        }
        if (r[0] != "NEXT") return false;
        current = node;
    }
    if (successor.empty()) {
        logLine("[DHT] join failed: could not find my successor");
        return false;
    }

    routing.setSuccessor(successor);
    routing.clearPredecessor();
    logLine("[DHT] joined the ring: my successor is " + successor.toString());

    // Tell the successor about me. It replies with the metadata for the keys
    // that are now mine, so the index moves along with the responsibility.
    if (ask(successor, "NOTIFY " + me.wire(), reply)) {
        vector<string> lines = splitLines(reply);
        if (lines.size() > 1) {
            string bulk;
            for (size_t i = 1; i < lines.size(); i++) bulk += lines[i] + "\n";
            int taken = index.applyBulk(bulk);
            if (taken > 0) {
                logLine("[DHT] received " + to_string(taken) + " metadata record(s) I am now responsible for");
            }
        }
    }

    // Fill the whole finger table once now. Without this the table would only
    // become correct after one full refresh cycle, and early lookups would
    // crawl along the successor chain instead of taking shortcuts.
    refreshAllFingers();

    running = true;
    maintenance = thread(&DhtNode::maintenanceLoop, this);
    return true;
}

// Graceful leave: nothing is lost and the ring closes behind us.
void DhtNode::leaveRing() {
    NodeInfo me = routing.getSelf();
    NodeInfo successor = routing.getSuccessor();
    if (successor.sameAs(me)) return;   // alone: nothing to hand over

    NodeInfo predecessor;
    bool hasPredecessor = routing.getPredecessor(predecessor);
    string reply;

    // 1. give the keys I own to my successor, which becomes responsible for them
    string owned = exportOwnedKeys();
    if (!owned.empty()) ask(successor, "BULK\n" + owned, reply);

    // 2. close the ring: my successor's new predecessor is my predecessor ...
    ask(successor, hasPredecessor ? ("LEAVING " + predecessor.wire()) : string("LEAVING NONE"), reply);
    // ... and my predecessor's new successor is my successor
    if (hasPredecessor) ask(predecessor, "SET_SUCC " + successor.wire(), reply);

    logLine("[DHT] left the ring (handed " + to_string(index.countFiles()) + " file record(s) to " +
            successor.toString() + ")");
}

// ===================== small accessors =====================

NodeInfo DhtNode::getSelf() {
    return routing.getSelf();
}

Address DhtNode::getTransferAddress() {
    return transferAddr;
}

RoutingTable& DhtNode::getRouting() {
    return routing;
}

DistributedIndex& DhtNode::getIndex() {
    return index;
}

// ===================== talking to other nodes =====================

bool DhtNode::ask(const NodeInfo& target, const string& request, string& reply) {
    // A node often has to ask "itself" (it may be its own successor, or the
    // responsible node for a key). Doing that over a socket would mean this
    // thread waits for a thread of the same process, so answer it directly.
    if (target.sameAs(routing.getSelf())) {
        reply = handleRequest(request);
        return true;
    }

    Socket sock;
    if (!sock.connectTo(target.ip, target.port, DHT_CONNECT_TIMEOUT_SEC)) return false;
    sock.setTimeout(DHT_IO_TIMEOUT_SEC);
    messagesSent++;
    if (!sock.sendMessage(request)) return false;
    return sock.recvMessage(reply);
}

// ===================== lookup (the heart of the DHT) =====================

NodeInfo DhtNode::lookup(unsigned int key, int& hops, vector<string>& trace) {
    NodeInfo current = routing.getSelf();
    hops = 0;

    for (int step = 0; step < MAX_LOOKUP_STEPS; step++) {
        string reply;
        if (!ask(current, "FIND_STEP " + to_string(key), reply)) {
            // The node we were routed to is dead. Drop it from the routing
            // table right now, so the caller's retry takes a different path
            // instead of walking into the same dead node again.
            trace.push_back("node " + current.toString() + " did not answer, forgetting it");
            routing.forgetNode(current);
            return NodeInfo();
        }

        vector<string> w = splitWords(reply);
        NodeInfo node;
        if (w.size() < 4 || !parseNode(w, 1, node)) return NodeInfo();

        if (w[0] == "SUCC") return node;         // this node is responsible
        if (w[0] != "NEXT") return NodeInfo();

        hops++;
        trace.push_back("hop " + to_string(hops) + ": " + current.toString() + " -> " + node.toString());
        current = node;
    }

    trace.push_back("gave up after " + to_string(MAX_LOOKUP_STEPS) + " steps (ring looks broken)");
    return NodeInfo();
}

// One routing step, answered by the node that receives FIND_STEP.
string DhtNode::findStep(unsigned int key) {
    NodeInfo me = routing.getSelf();
    NodeInfo successor = routing.getSuccessor();
    NodeInfo predecessor;

    // I am alone, so every key is mine
    if (successor.sameAs(me)) return "SUCC " + me.wire();

    // A node owns the keys in (predecessor, me]
    if (key == me.id) return "SUCC " + me.wire();
    if (routing.getPredecessor(predecessor) && inRangeExclIncl(key, predecessor.id, me.id)) {
        return "SUCC " + me.wire();
    }

    // The key belongs to the node right after me
    if (inRangeExclIncl(key, me.id, successor.id)) return "SUCC " + successor.wire();

    // Otherwise jump as far as a finger allows without passing the key.
    NodeInfo next = routing.closestPrecedingNode(key);
    // No finger helps (fingers are empty or stale): fall back to stepping along
    // the successor chain. Slower, but it always makes progress, which is why a
    // wrong finger table can never break a lookup.
    if (next.sameAs(me)) return "NEXT " + successor.wire();
    return "NEXT " + next.wire();
}

// ===================== message handling =====================

void DhtNode::handleConnection(Socket& sock) {
    sock.setTimeout(DHT_IO_TIMEOUT_SEC);
    string request;
    while (sock.recvMessage(request)) {
        if (!sock.sendMessage(handleRequest(request))) return;
    }
}

string DhtNode::handleRequest(const string& request) {
    messagesReceived++;

    // Only BULK carries several lines; every other request is a single line.
    vector<string> lines = splitLines(request);
    if (lines.empty()) return "ERROR empty request";
    vector<string> w = splitWords(lines[0]);
    if (w.empty()) return "ERROR empty request";
    string verb = w[0];

    if (verb == "PING") return "PONG " + to_string(routing.getSelf().id);

    if (verb == "FIND_STEP" && w.size() == 2) {
        long long key = 0;
        if (!parseInt(w[1], key) || key < 0 || key >= (long long)ID_SPACE) return "ERROR bad key";
        return findStep((unsigned int)key);
    }

    if (verb == "GET_PRED") {
        NodeInfo predecessor;
        if (!routing.getPredecessor(predecessor)) return "NONE";
        return "NODE " + predecessor.wire();
    }

    if (verb == "GET_SUCCLIST") {
        string out = "NODES";
        vector<NodeInfo> list = routing.getSuccessorList();
        for (size_t i = 0; i < list.size(); i++) out += " " + list[i].wire();
        return out;
    }

    if (verb == "NOTIFY") return handleNotify(w);

    if (verb == "PUT") return index.applyPut(w) ? "OK" : "ERROR rejected";

    if (verb == "GET" && w.size() == 2) return handleGet(w[1]);

    if (verb == "DROP" && w.size() == 3) {
        long long peerId = 0;
        if (!parseInt(w[2], peerId)) return "ERROR bad peer id";
        index.removePeer(w[1], (unsigned int)peerId);
        return "OK";
    }

    if (verb == "BULK") {
        string rest;
        for (size_t i = 1; i < lines.size(); i++) rest += lines[i] + "\n";
        index.applyBulk(rest);
        return "OK";
    }

    // My predecessor is leaving and tells me who comes before it
    if (verb == "LEAVING") {
        NodeInfo newPredecessor;
        if (w.size() == 2 && w[1] == "NONE") routing.clearPredecessor();
        else if (parseNode(w, 1, newPredecessor)) routing.setPredecessor(newPredecessor);
        return "OK";
    }

    // My successor is leaving and tells me who comes after it
    if (verb == "SET_SUCC") {
        NodeInfo newSuccessor;
        if (parseNode(w, 1, newSuccessor)) routing.setSuccessor(newSuccessor);
        return "OK";
    }

    return "ERROR unknown request";
}

// Another node claims to be my predecessor.
string DhtNode::handleNotify(const vector<string>& w) {
    NodeInfo candidate;
    if (!parseNode(w, 1, candidate)) return "ERROR bad node";

    NodeInfo me = routing.getSelf();
    if (candidate.sameAs(me)) return "OK";

    NodeInfo predecessor;
    bool hasPredecessor = routing.getPredecessor(predecessor);

    // Accept only if the candidate really sits between my predecessor and me.
    // This is what stops a stale or lying node from inserting itself, and it
    // also means a repeated NOTIFY from the same node changes nothing.
    bool accept = !hasPredecessor || inRangeExclExcl(candidate.id, predecessor.id, me.id);
    if (!accept) return "OK";

    routing.setPredecessor(candidate);
    // If I was alone, my new predecessor is also my successor: a ring of two.
    if (routing.getSuccessor().sameAs(me)) routing.setSuccessor(candidate);
    logLine("[DHT] new predecessor " + candidate.toString());

    // The candidate now owns the keys before it, so hand that metadata over.
    // I keep my copy as a replica; it expires on its own if it is not refreshed.
    string handoff = index.exportComplement(candidate.id, me.id);
    if (handoff.empty()) return "OK";
    return "OK\n" + handoff;
}

string DhtNode::handleGet(const string& fileName) {
    IndexEntry entry;
    if (!index.fetch(fileName, entry)) return "NOTFOUND";

    string out = "FOUND " + to_string(entry.meta.size) + " " + entry.meta.sha1 + " " +
                 to_string(entry.meta.pieceHashes.size());
    for (size_t i = 0; i < entry.meta.pieceHashes.size(); i++) out += "\n" + entry.meta.pieceHashes[i];
    for (auto& p : entry.peers) {
        out += "\nPEER " + to_string(p.second.nodeId) + " " + p.second.ip + " " +
               to_string(p.second.transferPort);
    }
    return out;
}

// ===================== periodic repair =====================

void DhtNode::maintenanceLoop() {
    int round = 0;
    while (running) {
        this_thread::sleep_for(chrono::milliseconds(MAINTENANCE_MS));
        if (!running) break;

        round++;
        stabilize();
        fixOneFinger();
        checkPredecessor();
        if (round % REPLICATE_EVERY_ROUNDS == 0) replicateToSuccessor();
        if (round % EXPIRE_EVERY_ROUNDS == 0) index.expireOld();
    }
}

// Make sure successors[0] is really the next node clockwise, and tell it about me.
void DhtNode::stabilize() {
    NodeInfo me = routing.getSelf();
    NodeInfo successor = routing.getSuccessor();

    if (successor.sameAs(me)) {
        // I think I am alone. If somebody has announced itself as my
        // predecessor, the ring has two nodes and that node is my successor too.
        NodeInfo predecessor;
        if (routing.getPredecessor(predecessor)) routing.setSuccessor(predecessor);
        return;
    }

    string reply;
    if (!ask(successor, "GET_PRED", reply)) {
        // FAILURE RECOVERY: the successor is gone. Promote the next node from
        // the successor list, which is exactly why that list is kept.
        routing.forgetNode(successor);
        NodeInfo promoted = routing.getSuccessor();
        logLine("[DHT] successor " + successor.toString() + " stopped answering, now using " +
                promoted.toString());
        // Shortcuts may also have pointed at nodes behind the dead one
        refreshAllFingers();
        return;
    }

    // If my successor's predecessor sits between us, it is my real successor.
    // This is how a newly joined node gets picked up by the node before it.
    vector<string> w = splitWords(reply);
    NodeInfo between;
    if (w.size() >= 4 && w[0] == "NODE" && parseNode(w, 1, between)) {
        if (!between.sameAs(me) && inRangeExclExcl(between.id, me.id, successor.id)) {
            routing.setSuccessor(between);
            successor = between;
            // The ring around me changed, so shortcuts pointing at the old
            // layout (or at myself, from when I was alone) are now stale.
            refreshAllFingers();
        }
    }

    // Tell my successor I am here; it may hand me keys in return.
    if (ask(successor, "NOTIFY " + me.wire(), reply)) {
        vector<string> lines = splitLines(reply);
        if (lines.size() > 1) {
            string bulk;
            for (size_t i = 1; i < lines.size(); i++) bulk += lines[i] + "\n";
            index.applyBulk(bulk);
        }
    }

    // Keep the backup successors fresh
    if (ask(successor, "GET_SUCCLIST", reply)) {
        vector<string> words = splitWords(reply);
        if (!words.empty() && words[0] == "NODES") {
            vector<NodeInfo> list;
            for (size_t i = 1; i + 2 < words.size(); i += 3) {
                NodeInfo node;
                if (parseNode(words, (int)i, node)) list.push_back(node);
            }
            routing.updateSuccessorList(list);
        }
    }
}

// Refresh one finger per round, cycling through all of them.
void DhtNode::fixOneFinger() {
    NodeInfo me = routing.getSelf();
    nextFinger = (nextFinger + 1) % ID_BITS;

    unsigned int target = addOnRing(me.id, 1u << nextFinger);
    int hops = 0;
    vector<string> trace;
    NodeInfo owner = lookup(target, hops, trace);
    if (!owner.empty()) routing.setFinger(nextFinger, owner);
}

// Recompute every shortcut. Used at join time and whenever the successor
// changes; the periodic one-per-round refresh then keeps them fresh cheaply.
void DhtNode::refreshAllFingers() {
    NodeInfo me = routing.getSelf();
    for (int i = 0; i < ID_BITS; i++) {
        int hops = 0;
        vector<string> trace;
        NodeInfo owner = lookup(addOnRing(me.id, 1u << i), hops, trace);
        if (!owner.empty()) routing.setFinger(i, owner);
    }
}

void DhtNode::checkPredecessor() {
    NodeInfo predecessor;
    if (!routing.getPredecessor(predecessor)) return;

    string reply;
    if (!ask(predecessor, "PING", reply)) {
        // Forgetting it lets the next NOTIFY install the correct predecessor,
        // and meanwhile I answer for the wider key range, so lookups still work.
        routing.clearPredecessor();
        logLine("[DHT] predecessor " + predecessor.toString() + " stopped answering, forgetting it");
    }
}

// The keys in (predecessor, me] - the ones this node is responsible for.
string DhtNode::exportOwnedKeys() {
    NodeInfo me = routing.getSelf();
    NodeInfo predecessor;
    if (!routing.getPredecessor(predecessor)) return index.exportRange(me.id, me.id);   // all of them
    return index.exportRange(predecessor.id, me.id);
}

// REPLICATION: push the metadata I own to my successor. If I crash, a lookup
// for one of my keys lands on that successor, which already has the records.
// Only OWNED keys are pushed, so replicas do not keep spreading around the ring.
void DhtNode::replicateToSuccessor() {
    NodeInfo me = routing.getSelf();
    NodeInfo successor = routing.getSuccessor();
    if (successor.sameAs(me)) return;

    string owned = exportOwnedKeys();
    if (owned.empty()) return;

    string reply;
    ask(successor, "BULK\n" + owned, reply);
}

// ===================== statistics and ring walk =====================

void DhtNode::recordLookup(int hops, long long milliseconds) {
    lookupCount++;
    hopTotal += hops;
    lookupMsTotal += milliseconds;
}

string DhtNode::describeStats() {
    long long lookups = lookupCount;
    string out = "DHT statistics for this node\n";
    out += "  overlay messages sent     : " + to_string((long long)messagesSent) + "\n";
    out += "  overlay messages received : " + to_string((long long)messagesReceived) + "\n";
    out += "  file lookups done         : " + to_string(lookups) + "\n";
    if (lookups > 0) {
        // Printed with one decimal place without using iostream formatting
        long long hopTenths = (long long)hopTotal * 10 / lookups;
        out += "  average hops per lookup   : " + to_string(hopTenths / 10) + "." + to_string(hopTenths % 10) + "\n";
        out += "  average lookup time       : " + to_string((long long)lookupMsTotal / lookups) + " ms\n";
    }
    out += "  metadata records stored   : " + to_string(index.countFiles());
    return out;
}

// Follow the successor pointers all the way around, which proves the ring is
// closed and shows how the ids are spread over the id space.
string DhtNode::walkRing() {
    NodeInfo me = routing.getSelf();
    NodeInfo current = me;
    string out = "Ring, walking clockwise from me:";

    for (int i = 0; i < MAX_LOOKUP_STEPS; i++) {
        out += "\n  " + to_string(i + 1) + ". " + current.toString();
        if (i == 0) out += "   <- this peer";

        string reply;
        if (!ask(current, "GET_SUCCLIST", reply)) {
            out += "   (unreachable)";
            break;
        }
        vector<string> w = splitWords(reply);
        NodeInfo next;
        if (w.size() < 4 || w[0] != "NODES" || !parseNode(w, 1, next)) break;
        if (next.sameAs(me)) {
            out += "\n  -> back to " + me.toString() + ", the ring is closed (" + to_string(i + 1) + " node(s))";
            break;
        }
        current = next;
    }
    return out;
}
