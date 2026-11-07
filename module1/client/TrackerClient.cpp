// TrackerClient.cpp - request/reply with the tracker + automatic failover
#include "TrackerClient.h"

using namespace std;

TrackerClient::TrackerClient(vector<Address> trackerList) {
    trackers = trackerList;
    current = 0;
}

bool TrackerClient::connectToAnyTracker() {
    lock_guard<mutex> lock(mtx);
    return tryConnect();
}

// Try every tracker once, starting with "current"
bool TrackerClient::tryConnect() {
    for (int i = 0; i < (int)trackers.size(); i++) {
        int index = (current + i) % trackers.size();
        if (sock.connectTo(trackers[index].ip, trackers[index].port, 3)) {
            sock.setTimeout(15);   // a tracker that hangs is treated like a dead one
            current = index;
            logLine("[INFO] Connected to tracker " + trackers[index].toString());
            return true;
        }
    }
    return false;
}

bool TrackerClient::sendAndReceive(const string& command, string& reply) {
    if (sock.sendMessage(command) && sock.recvMessage(reply)) return true;
    sock.closeSocket();
    return false;
}

string TrackerClient::request(const string& command) {
    lock_guard<mutex> lock(mtx);   // one request/reply at a time

    // Normal case: tracker is alive
    string reply;
    if (sock.isValid() && sendAndReceive(command, reply)) return reply;

    // Failover: this tracker is gone -> start with the NEXT one
    current = (current + 1) % trackers.size();
    if (!tryConnect()) return "ERROR: No tracker is reachable";

    // The new tracker doesn't know our connection yet -> log in again
    if (!loginCommand.empty()) {
        string loginReply;
        if (sendAndReceive(loginCommand, loginReply)) {
            logLine("[INFO] Re-login after failover: " + loginReply);
        }
    }

    // Retry the original request once
    if (sendAndReceive(command, reply)) return reply;
    return "ERROR: Lost connection to tracker";
}

void TrackerClient::saveLogin(const string& command) {
    lock_guard<mutex> lock(mtx);
    loginCommand = command;
}

void TrackerClient::forgetLogin() {
    lock_guard<mutex> lock(mtx);
    loginCommand = "";
}
