#pragma once
// =====================================================================
// TrackerClient - the client's connection to the tracker system
// =====================================================================
// 1. THREAD-SAFE: the command loop and background downloads (auto-seeding)
//    both use it. A mutex makes each "send request + read reply" one unit,
//    so two threads never get each other's replies.
//
// 2. FAILOVER: if the tracker dies, the next request automatically:
//      connects to the next tracker in tracker_info.txt
//      -> logs in again with the saved login command
//      -> retries the request.
//    The user just keeps typing commands and doesn't notice.

#include "Socket.h"
#include "Utils.h"

#include <mutex>
#include <string>
#include <vector>

using namespace std;

class TrackerClient {
public:
    TrackerClient(vector<Address> trackerList);

    bool connectToAnyTracker();
    string request(const string& command);   // send a command, return the reply

    // Remember / forget the login command, to replay it after a failover
    void saveLogin(const string& loginCommand);
    void forgetLogin();

private:
    bool tryConnect();                                       // mtx must be held
    bool sendAndReceive(const string& command, string& reply);

    mutex mtx;
    vector<Address> trackers;
    int current;          // index of the tracker we use now
    Socket sock;
    string loginCommand;  // empty = not logged in
};
