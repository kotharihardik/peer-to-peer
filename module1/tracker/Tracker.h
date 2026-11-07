#pragma once
// =====================================================================
// Tracker - the tracker server (inherits TcpServer)
// =====================================================================
// A tracker stores only METADATA: users, groups, which files exist and who
// has them. The file data itself never goes through the tracker - clients
// send pieces directly to each other. (That is the "Napster model":
// central index, peer-to-peer data transfer.)
//
// Parts of the tracker:
//   networking     -> TcpServer (base class, one thread per client)
//   sessions       -> Session, one per connection (who is logged in on it)
//   business logic -> TrackerState
//   replication    -> TrackerSynchronizer

#include "TcpServer.h"
#include "TrackerState.h"
#include "TrackerSynchronizer.h"
#include "Utils.h"

#include <string>
#include <vector>

using namespace std;

// Who is logged in on one connection.
// Stored on the connection thread's stack (not in a global fd->user map), so
// it ends with the connection and can never be inherited via a reused fd.
struct Session {
    string userId;   // empty = not logged in
};

class Tracker : public TcpServer {
public:
    Tracker(vector<Address> trackers, int myIndex);
    ~Tracker();

    bool start();
    void stop();

protected:
    // One call per client connection (runs on its own thread)
    void handleConnection(Socket& sock) override;

private:
    string executeCommand(const vector<string>& args, Session& session);
    string finish(const Reply& reply);   // sync the change, return the text

    Address self;
    TrackerState state;          // declared BEFORE sync, because sync uses it
    TrackerSynchronizer sync;
};
