#pragma once
// =====================================================================
// TrackerSynchronizer - keeps the two trackers' data the same
// =====================================================================
// HOW SYNC WORKS:
//
//  1. RECOVERY ON START: a tracker that (re)starts first asks the other tracker
//     for a full SNAPSHOT, before accepting any client. So it never serves
//     empty or old data.
//
//  2. LIVE UPDATES: every change (new user, join, upload...) is turned into a
//     sync op and pushed to the other tracker over ONE persistent TCP
//     connection. One connection = ops arrive in the same order they happened.
//
//  3. SELF-HEALING: whenever that connection (re)connects, the FIRST thing sent
//     is a full snapshot. So updates lost while the other tracker was down are
//     repaired automatically. Ops are idempotent, so overlap is harmless.
//
// While the peer is unreachable no messages are queued; the link retries every
// second and the snapshot on reconnect brings the peer up to date.
// Partition limitation: snapshots are merged, so a deletion made on one side
// (leave group, stop share) can be restored by the other side's snapshot.

#include "Socket.h"
#include "TcpServer.h"
#include "TrackerState.h"
#include "Utils.h"

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace std;

// ---------------------------------------------------------------------
// PeerLink - OUTGOING connection to one other tracker, with its own thread
// ---------------------------------------------------------------------
// Producer-consumer: client threads add ops (addOps), the link thread sends
// them. Queue + mutex + condition_variable, so client threads never block on
// the network.
class PeerLink {
public:
    PeerLink(TrackerState& state, Address peer);
    ~PeerLink();

    void start();
    void stop();
    void addOps(const string& ops);   // called by client threads

private:
    void run();                        // the link thread
    bool connectAndSendSnapshot();

    TrackerState& state;
    Address peer;
    Socket sock;
    thread worker;

    mutex mtx;
    condition_variable cv;             // wakes the link thread when ops arrive
    deque<string> queue;               // ops waiting to be sent
    bool stopping;
};

// ---------------------------------------------------------------------
// TrackerSynchronizer - INCOMING side (a TcpServer on the sync port)
// plus one PeerLink per other tracker
// ---------------------------------------------------------------------
class TrackerSynchronizer : public TcpServer {
public:
    TrackerSynchronizer(TrackerState& state, Address self, vector<Address> peers);
    ~TrackerSynchronizer();

    bool pullSnapshot();              // rule 1 (called at startup)
    bool start();                     // start sync server + peer links
    void stop();
    void broadcast(const string& ops); // rule 2 (called after every change)

protected:
    // Runs for each connection coming FROM another tracker
    void handleConnection(Socket& sock) override;

private:
    TrackerState& state;
    Address self;
    vector<Address> peers;
    vector<unique_ptr<PeerLink>> links;
};
