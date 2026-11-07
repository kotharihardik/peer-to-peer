// TrackerSynchronizer.cpp - tracker-to-tracker replication (snapshot + live ops)
#include "TrackerSynchronizer.h"
#include "Protocol.h"

#include <chrono>

using namespace std;

// Sync port of a tracker = its client port + 1000
static Address syncAddressOf(const Address& tracker) {
    return Address(tracker.ip, tracker.port + SYNC_PORT_OFFSET);
}

// =============================== PeerLink ===============================

PeerLink::PeerLink(TrackerState& s, Address p) : state(s) {
    peer = p;
    stopping = false;
}

PeerLink::~PeerLink() {
    stop();
}

void PeerLink::start() {
    worker = thread(&PeerLink::run, this);
}

void PeerLink::stop() {
    {
        lock_guard<mutex> lock(mtx);
        stopping = true;
    }
    cv.notify_one();   // wake the thread so it sees "stopping"
    if (worker.joinable()) worker.join();
}

// Producer side: just put the ops in the queue and wake the link thread
void PeerLink::addOps(const string& ops) {
    {
        lock_guard<mutex> lock(mtx);
        queue.push_back(ops);
    }
    cv.notify_one();
}

// Connect to the other tracker and immediately push our full state (rule 3)
bool PeerLink::connectAndSendSnapshot() {
    Address target = syncAddressOf(peer);
    bool connected = sock.connectTo(target.ip, target.port, 2);

    // IMPORTANT ORDER: clear the queue BEFORE taking the snapshot.
    // Every op already in the queue was already applied to our state, so the
    // snapshot contains it. Ops added after this point stay in the queue.
    // (Snapshot first, then clear, could throw away an op that is not in it.)
    {
        lock_guard<mutex> lock(mtx);
        queue.clear();
    }
    if (!connected) return false;

    sock.setTimeout(10);
    if (!sock.sendMessage("OPS\n" + state.snapshot())) {
        sock.closeSocket();
        return false;
    }
    logLine("[SYNC] Connected to tracker " + peer.toString());
    return true;
}

// Consumer side: the link thread's main loop
void PeerLink::run() {
    bool wasConnected = false;
    while (true) {
        // 1. Make sure we are connected. isClosedByPeer() notices within ~1 s
        //    that the other tracker died, even when we have nothing to send.
        if (!sock.isValid() || sock.isClosedByPeer()) {
            if (wasConnected) logLine("[SYNC] Lost connection to tracker " + peer.toString());
            sock.closeSocket();
            wasConnected = connectAndSendSnapshot();
        }

        // 2. Wait until there are ops to send, we must stop, or 1 second passed
        deque<string> batch;
        {
            unique_lock<mutex> lock(mtx);
            // Only sleep if there is nothing to do (else a wake-up that came
            // before we started waiting would be missed)
            if (queue.empty() && !stopping) cv.wait_for(lock, chrono::seconds(1));
            if (stopping) return;
            if (!sock.isValid()) continue;   // not connected: retry next round
            batch.swap(queue);               // take all waiting ops at once
        }

        // 3. Send them (outside the lock, so client threads are never blocked by the network)
        for (auto& ops : batch) {
            if (!sock.sendMessage("OPS\n" + ops)) {
                sock.closeSocket();   // lost ops are repaired by the snapshot on reconnect
                break;
            }
        }
    }
}

// ========================== TrackerSynchronizer ==========================

TrackerSynchronizer::TrackerSynchronizer(TrackerState& s, Address me, vector<Address> others) : state(s) {
    self = me;
    peers = others;
}

TrackerSynchronizer::~TrackerSynchronizer() {
    stop();
}

// Rule 1: ask any running tracker for its full state
bool TrackerSynchronizer::pullSnapshot() {
    for (auto& peer : peers) {
        Address target = syncAddressOf(peer);
        Socket sock;
        if (!sock.connectTo(target.ip, target.port, 2)) continue;
        sock.setTimeout(10);

        string snapshot;
        if (sock.sendMessage("SNAPSHOT_REQUEST") && sock.recvMessage(snapshot)) {
            state.applyOps(snapshot);
            logLine("[SYNC] Recovered state from tracker " + peer.toString());
            return true;
        }
    }
    return false;
}

bool TrackerSynchronizer::start() {
    Address mySync = syncAddressOf(self);
    if (!startServer("0.0.0.0", mySync.port)) return false;
    logLine("[SYNC] Listening for trackers on port " + to_string(mySync.port));

    for (auto& peer : peers) {
        links.push_back(unique_ptr<PeerLink>(new PeerLink(state, peer)));
        links.back()->start();
    }
    return true;
}

void TrackerSynchronizer::stop() {
    for (auto& link : links) link->stop();
    links.clear();
    stopServer();
}

// Rule 2: send a change to every other tracker (non-blocking, just queues it)
void TrackerSynchronizer::broadcast(const string& ops) {
    if (ops.empty()) return;
    for (auto& link : links) link->addOps(ops);
}

// Incoming connection from another tracker. Two kinds of messages:
//   "SNAPSHOT_REQUEST" -> reply with our full state
//   "OPS\n..."         -> apply the changes
// Note: we never re-broadcast ops received here, so there are no sync loops.
void TrackerSynchronizer::handleConnection(Socket& sock) {
    string msg;
    while (sock.recvMessage(msg)) {
        if (msg == "SNAPSHOT_REQUEST") {
            if (!sock.sendMessage(state.snapshot())) return;
        } else if (msg.substr(0, 4) == "OPS\n") {
            state.applyOps(msg.substr(4));
        }
    }
}
