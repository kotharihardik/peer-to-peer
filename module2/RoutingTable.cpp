// RoutingTable.cpp
#include "RoutingTable.h"
#include "Hashing.h"

using namespace std;

RoutingTable::RoutingTable() {
    fingers.resize(ID_BITS);
}

void RoutingTable::setSelf(const NodeInfo& me) {
    lock_guard<mutex> lock(mtx);
    self = me;
    // A brand new node is alone: it is its own successor and every finger
    // points at itself. Lookups then correctly answer "I am responsible".
    successors.clear();
    successors.push_back(me);
    for (int i = 0; i < ID_BITS; i++) fingers[i] = me;
}

NodeInfo RoutingTable::getSelf() {
    lock_guard<mutex> lock(mtx);
    return self;
}

NodeInfo RoutingTable::getSuccessor() {
    lock_guard<mutex> lock(mtx);
    if (successors.empty()) return self;
    return successors[0];
}

vector<NodeInfo> RoutingTable::getSuccessorList() {
    lock_guard<mutex> lock(mtx);
    return successors;
}

void RoutingTable::setSuccessor(const NodeInfo& node) {
    lock_guard<mutex> lock(mtx);
    if (successors.empty()) successors.push_back(node);
    else successors[0] = node;
}

// My list = my successor, then its successors, skipping myself and duplicates.
void RoutingTable::updateSuccessorList(const vector<NodeInfo>& fromSuccessor) {
    lock_guard<mutex> lock(mtx);
    if (successors.empty()) return;

    vector<NodeInfo> rebuilt;
    rebuilt.push_back(successors[0]);
    for (size_t i = 0; i < fromSuccessor.size(); i++) {
        if ((int)rebuilt.size() >= SUCCESSOR_LIST_SIZE) break;
        if (fromSuccessor[i].sameAs(self)) continue;

        bool alreadyThere = false;
        for (size_t j = 0; j < rebuilt.size(); j++) {
            if (rebuilt[j].sameAs(fromSuccessor[i])) alreadyThere = true;
        }
        if (!alreadyThere) rebuilt.push_back(fromSuccessor[i]);
    }
    successors = rebuilt;
}

NodeInfo RoutingTable::dropDeadSuccessor() {
    lock_guard<mutex> lock(mtx);
    if (!successors.empty()) successors.erase(successors.begin());
    if (successors.empty()) successors.push_back(self);   // alone again
    return successors[0];
}

bool RoutingTable::getPredecessor(NodeInfo& out) {
    lock_guard<mutex> lock(mtx);
    if (predecessor.empty()) return false;
    out = predecessor;
    return true;
}

void RoutingTable::setPredecessor(const NodeInfo& node) {
    lock_guard<mutex> lock(mtx);
    predecessor = node;
}

void RoutingTable::clearPredecessor() {
    lock_guard<mutex> lock(mtx);
    predecessor = NodeInfo();
}

void RoutingTable::setFinger(int i, const NodeInfo& node) {
    lock_guard<mutex> lock(mtx);
    if (i >= 0 && i < ID_BITS) fingers[i] = node;
}

NodeInfo RoutingTable::getFinger(int i) {
    lock_guard<mutex> lock(mtx);
    if (i < 0 || i >= ID_BITS) return self;
    return fingers[i];
}

// Called when a request to this node timed out. Fingers fall back to self,
// which is always safe: findStep() then walks the successor chain instead.
void RoutingTable::forgetNode(const NodeInfo& dead) {
    lock_guard<mutex> lock(mtx);
    if (dead.sameAs(self)) return;

    for (int i = 0; i < ID_BITS; i++) {
        if (fingers[i].sameAs(dead)) fingers[i] = self;
    }
    for (size_t i = 0; i < successors.size(); i++) {
        if (successors[i].sameAs(dead)) {
            successors.erase(successors.begin() + i);
            break;
        }
    }
    if (successors.empty()) successors.push_back(self);
    if (predecessor.sameAs(dead)) predecessor = NodeInfo();
}

// Search the fingers from the FURTHEST one backwards, so we take the biggest
// jump that does not overshoot the key.
NodeInfo RoutingTable::closestPrecedingNode(unsigned int key) {
    lock_guard<mutex> lock(mtx);
    for (int i = ID_BITS - 1; i >= 0; i--) {
        if (fingers[i].empty()) continue;
        if (fingers[i].sameAs(self)) continue;
        if (inRangeExclExcl(fingers[i].id, self.id, key)) return fingers[i];
    }
    return self;
}

string RoutingTable::describe() {
    lock_guard<mutex> lock(mtx);
    string out = "Node id      : " + to_string(self.id) + "  (" + self.ip + ":" + to_string(self.port) + ")\n";

    out += "Predecessor  : " + (predecessor.empty() ? string("unknown") : predecessor.toString()) + "\n";

    out += "Successors   : ";
    if (successors.empty()) out += "none";
    for (size_t i = 0; i < successors.size(); i++) {
        if (i > 0) out += ", ";
        out += successors[i].toString();
    }
    out += "\nKeys I own   : (" + (predecessor.empty() ? string("?") : to_string(predecessor.id)) + ", " +
           to_string(self.id) + "]\n";

    // Only print a finger when it differs from the previous one, otherwise the
    // list is 16 identical lines in a small ring.
    out += "Finger table :\n";
    unsigned int previous = ID_SPACE + 1;
    for (int i = 0; i < ID_BITS; i++) {
        if (fingers[i].empty()) continue;
        if (fingers[i].id == previous) continue;
        previous = fingers[i].id;
        out += "  +2^" + to_string(i) + " (key " + to_string(addOnRing(self.id, 1u << i)) + ") -> " +
               fingers[i].toString() + "\n";
    }
    out.pop_back();
    return out;
}
