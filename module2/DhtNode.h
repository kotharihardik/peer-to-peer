#pragma once
// =====================================================================
// DhtNode - one node of the Chord-like overlay
// =====================================================================
// This is the class that replaces the tracker. It does three jobs:
//
//   1. SERVER  - answers overlay messages from other nodes (inherits TcpServer,
//                exactly like the tracker did, so one thread per connection).
//   2. ROUTER  - lookup(key) walks the overlay to find the node responsible
//                for a key, counting hops.
//   3. STORE   - keeps its slice of the distributed index (DistributedIndex).
//
// LOOKUP IS ITERATIVE: the node that wants the answer asks one node, is told
// "ask this one next", and repeats. The alternative (recursive: each node
// forwards the query itself) would leave a chain of threads blocked waiting on
// each other, and hop counting would be harder. Iterative keeps all the state
// in one place and makes the [LOOKUP] trace easy to print.
//
// MAINTENANCE THREAD: one background thread repeats Chord's repair routines:
//   stabilize()        - check my successor is still right, tell it about me
//   fixOneFinger()     - refresh one finger table entry per tick
//   checkPredecessor() - forget a predecessor that stopped answering
//   replicate()        - push the keys I own to my successor as a backup
// Everything is repaired by repetition rather than by careful bookkeeping,
// which is what makes the ring self-healing after joins, leaves and crashes.
//
// WIRE PROTOCOL (all framed text, reusing common/Socket):
//   PING                       -> PONG <id>
//   FIND_STEP <key>            -> SUCC <node> (final) | NEXT <node> (keep going)
//   GET_PRED                   -> NODE <node> | NONE
//   GET_SUCCLIST               -> NODES <node> <node> ...
//   NOTIFY <node>              -> OK [+ PUT lines this node should now own]
//   PUT <key> <name> ...       -> OK       (publish / refresh metadata)
//   GET <name>                 -> FOUND ... | NOTFOUND
//   DROP <name> <peerId>       -> OK       (stop sharing)
//   BULK\n<PUT lines>          -> OK       (handoff or replication)
//   LEAVING <node> | NONE      -> OK       (my predecessor is leaving)
//   SET_SUCC <node>            -> OK       (my successor is leaving)

#include "DistributedIndex.h"
#include "RoutingTable.h"
#include "TcpServer.h"
#include "Utils.h"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

using namespace std;

// How often the maintenance thread runs one round
const int MAINTENANCE_MS = 500;
// Push the keys I own to my successor every N rounds (500 ms * 10 = 5 s)
const int REPLICATE_EVERY_ROUNDS = 10;
// Expire stale index entries every N rounds
const int EXPIRE_EVERY_ROUNDS = 20;
// Talking to another node: short timeouts, because a dead node must not stall us
const int DHT_CONNECT_TIMEOUT_SEC = 2;
const int DHT_IO_TIMEOUT_SEC = 5;
// Safety limit so a broken ring can never make a lookup spin forever
const int MAX_LOOKUP_STEPS = 64;

class DhtNode : public TcpServer {
public:
    DhtNode();
    ~DhtNode();

    // dhtAddr    = where this node listens for overlay messages
    // transferAddr = where its PeerServer serves file pieces (published in the index)
    bool start(const Address& dhtAddr, const Address& transferAddr);
    void stop();

    void createRing();                          // first peer: a ring of one
    bool joinRing(const Address& bootstrap);    // join through any existing peer
    void leaveRing();                           // graceful departure

    NodeInfo getSelf();
    Address getTransferAddress();
    RoutingTable& getRouting();
    DistributedIndex& getIndex();

    // Find the node responsible for a key. Fills hops and a readable trace.
    // Returns an empty NodeInfo if the overlay could not answer.
    NodeInfo lookup(unsigned int key, int& hops, vector<string>& trace);

    // Send one request to one node and read its reply. When the target is this
    // node the request is answered directly, without opening a socket to itself.
    bool ask(const NodeInfo& target, const string& request, string& reply);

    void recordLookup(int hops, long long milliseconds);
    string describeStats();
    string walkRing();

protected:
    void handleConnection(Socket& sock) override;

private:
    string handleRequest(const string& request);   // one message in, one reply out
    string findStep(unsigned int key);
    string handleNotify(const vector<string>& words);
    string handleGet(const string& fileName);

    void maintenanceLoop();
    void stabilize();
    void fixOneFinger();
    void refreshAllFingers();
    void checkPredecessor();
    void replicateToSuccessor();
    string exportOwnedKeys();

    RoutingTable routing;
    DistributedIndex index;
    Address transferAddr;

    thread maintenance;
    atomic<bool> running;
    int nextFinger;                 // only the maintenance thread touches this

    atomic<long long> messagesSent;
    atomic<long long> messagesReceived;
    atomic<long long> lookupCount;
    atomic<long long> hopTotal;
    atomic<long long> lookupMsTotal;
};
