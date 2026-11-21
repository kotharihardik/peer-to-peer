#pragma once
// =====================================================================
// RoutingTable - what one node remembers about the ring
// =====================================================================
// Three pieces of routing state:
//
// 1. SUCCESSOR LIST - the next few nodes clockwise. successors[0] is the real
//    successor; the extras exist only so that a node can survive its successor
//    dying (it just promotes the next one). This is what keeps the ring
//    connected, so correctness depends on it.
//
// 2. PREDECESSOR - the node before me. I own the keys in (predecessor, me].
//
// 3. FINGER TABLE - shortcuts across the ring: finger[i] is the node
//    responsible for (me + 2^i). Those powers of two are what turn a lookup
//    from "walk one node at a time" (O(N)) into "halve the remaining distance
//    every hop" (O(log N)).
//
// The fingers are only an OPTIMIZATION. If every finger were wrong, lookups
// would still find the right node by walking the successor chain - just
// slowly. That separation is why this design tolerates stale routing data.
//
// Every method locks the mutex: the DHT server threads and the maintenance
// thread read and write this table at the same time.

#include "NodeInfo.h"

#include <mutex>
#include <string>
#include <vector>

using namespace std;

// How many extra successors to remember for failure recovery
const int SUCCESSOR_LIST_SIZE = 3;

class RoutingTable {
public:
    RoutingTable();

    void setSelf(const NodeInfo& me);
    NodeInfo getSelf();

    // successors[0]. Returns self when this node is alone in the ring.
    NodeInfo getSuccessor();
    vector<NodeInfo> getSuccessorList();
    void setSuccessor(const NodeInfo& node);

    // Rebuild the list as [successor] + the successor's own list
    void updateSuccessorList(const vector<NodeInfo>& fromSuccessor);

    // The successor did not answer: forget it and promote the next one.
    // Returns the new successor (self if nothing is left).
    NodeInfo dropDeadSuccessor();

    bool getPredecessor(NodeInfo& out);
    void setPredecessor(const NodeInfo& node);
    void clearPredecessor();

    void setFinger(int i, const NodeInfo& node);
    NodeInfo getFinger(int i);

    // A node stopped answering: drop it from the fingers and from the
    // successor list so routing stops sending traffic into a black hole.
    void forgetNode(const NodeInfo& dead);

    // Chord's closest_preceding_node: the furthest finger that is still before
    // the key. Returns self when no finger helps.
    NodeInfo closestPrecedingNode(unsigned int key);

    // Multi-line text for the "info" command
    string describe();

private:
    mutex mtx;
    NodeInfo self;
    vector<NodeInfo> successors;   // successors[0] = immediate successor
    NodeInfo predecessor;          // empty() = unknown
    vector<NodeInfo> fingers;      // ID_BITS entries
};
