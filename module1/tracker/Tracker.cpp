// Tracker.cpp - receives client commands, checks login, calls TrackerState
#include "Tracker.h"

using namespace std;

// All trackers from tracker_info.txt except me
static vector<Address> otherTrackers(const vector<Address>& all, int myIndex) {
    vector<Address> others;
    for (int i = 0; i < (int)all.size(); i++) {
        if (i != myIndex) others.push_back(all[i]);
    }
    return others;
}

Tracker::Tracker(vector<Address> trackers, int myIndex)
    : sync(state, trackers[myIndex], otherTrackers(trackers, myIndex)) {
    self = trackers[myIndex];
}

Tracker::~Tracker() {
    stop();
}

// Startup order matters:
//  1. pull state from the other tracker (recovery)
//  2. start the sync server + links
//  3. only THEN accept clients -> no client ever sees an empty tracker
bool Tracker::start() {
    if (!sync.pullSnapshot()) logLine("[SYNC] No other tracker reachable, starting with empty state");

    if (!sync.start()) {
        logLine("[ERROR] Cannot open sync port");
        return false;
    }
    if (!startServer(self.ip, self.port)) {
        logLine("[ERROR] Cannot listen on " + self.toString());
        return false;
    }
    logLine("Tracker running at " + self.toString() + "  (type 'quit' to stop)");
    return true;
}

void Tracker::stop() {
    stopServer();   // client connections first
    sync.stop();
}

// Every change goes to the other tracker, then the text goes to the client
string Tracker::finish(const Reply& reply) {
    sync.broadcast(reply.syncOps);   // does nothing if syncOps is empty
    return reply.text;
}

// One thread per client: read command -> execute -> send reply, until disconnect
void Tracker::handleConnection(Socket& sock) {
    Session session;
    string request;
    while (sock.recvMessage(request)) {
        string reply = executeCommand(splitWords(request), session);
        if (!sock.sendMessage(reply)) break;
    }

    // Connection closed (logout, crash or network failure): end the session
    // and broadcast OFFLINE so this user is no longer offered as a seeder.
    if (!session.userId.empty()) {
        finish(state.logout(session.userId));
        logLine("[INFO] " + session.userId + " disconnected");
    }
}

// Command dispatcher: check arguments + login, then call TrackerState.
// The user id always comes from this connection's session, never from the
// message itself, so a client cannot act as another user.
string Tracker::executeCommand(const vector<string>& args, Session& session) {
    if (args.empty()) return "ERROR: Empty command";
    string cmd = args[0];
    int n = args.size();

    // ---------- commands allowed without login ----------
    if (cmd == "create_user") {
        if (n != 3) return "ERROR: Usage: create_user <user_id> <password>";
        return finish(state.createUser(args[1], args[2]));
    }
    if (cmd == "login") {
        // The client adds its own peer address: login <uid> <pwd> <peerIp> <peerPort>
        long long port;
        if (n != 5 || !parseInt(args[4], port)) return "ERROR: Usage: login <user_id> <password>";
        if (!session.userId.empty()) return "ERROR: Already logged in on this connection";
        Reply reply = state.login(args[1], args[2], args[3], (int)port);
        if (reply.text.substr(0, 7) == "SUCCESS") session.userId = args[1];
        return finish(reply);
    }
    if (cmd == "list_groups") {
        return finish(state.listGroups());
    }

    // ---------- everything below needs login ----------
    string uid = session.userId;
    if (uid.empty()) return "ERROR: Please login first";

    if (cmd == "logout") {
        session.userId = "";
        return finish(state.logout(uid));
    }
    if (cmd == "create_group") {
        if (n != 2) return "ERROR: Usage: create_group <group_id>";
        return finish(state.createGroup(uid, args[1]));
    }
    if (cmd == "join_group") {
        if (n != 2) return "ERROR: Usage: join_group <group_id>";
        return finish(state.joinGroup(uid, args[1]));
    }
    if (cmd == "leave_group") {
        if (n != 2) return "ERROR: Usage: leave_group <group_id>";
        return finish(state.leaveGroup(uid, args[1]));
    }
    if (cmd == "list_requests") {
        if (n != 2) return "ERROR: Usage: list_requests <group_id>";
        return finish(state.listRequests(uid, args[1]));
    }
    if (cmd == "accept_request") {
        if (n != 3) return "ERROR: Usage: accept_request <group_id> <user_id>";
        return finish(state.acceptRequest(uid, args[1], args[2]));
    }
    if (cmd == "list_files") {
        if (n != 2) return "ERROR: Usage: list_files <group_id>";
        return finish(state.listFiles(uid, args[1]));
    }
    if (cmd == "stop_share") {
        if (n != 3) return "ERROR: Usage: stop_share <group_id> <file_name>";
        return finish(state.stopShare(uid, args[1], args[2]));
    }
    if (cmd == "download_file") {
        if (n != 3) return "ERROR: Usage: download_file <group_id> <file_name>";
        return finish(state.downloadInfo(uid, args[1], args[2]));
    }
    if (cmd == "upload_file") {
        // The client hashes the file first and sends:
        // upload_file <gid> <file> <size> <sha1> <pieceCount> <hash1> ... <hashN>
        long long size, count;
        if (n < 6 || !parseInt(args[3], size) || !parseInt(args[5], count) || count < 0 || n != 6 + count) {
            return "ERROR: Malformed upload request";
        }
        vector<string> pieceHashes(args.begin() + 6, args.end());
        return finish(state.uploadFile(uid, args[1], args[2], size, args[4], pieceHashes));
    }
    return "ERROR: Unknown command '" + cmd + "'";
}
