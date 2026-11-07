// Client.cpp - command loop: decides which part of the client handles a command
#include "Client.h"

#include <iostream>

using namespace std;

static bool isSuccess(const string& reply) {
    return reply.substr(0, 7) == "SUCCESS";
}

// Members are built in the order they are declared in Client.h
Client::Client(Address me, vector<Address> trackers)
    : tracker(trackers), index(tracker), uploads(index, shared), peerServer(shared),
      downloads(index, uploads, me) {
    self = me;
}

bool Client::start() {
    if (!peerServer.start(self.port)) {
        logLine("[ERROR] Cannot listen for peers on port " + to_string(self.port));
        return false;
    }
    logLine("[INFO] Serving file pieces on port " + to_string(self.port));

    if (!tracker.connectToAnyTracker()) {
        logLine("[ERROR] No tracker is reachable");
        return false;
    }
    return true;
}

// Main loop: show prompt, read a line, execute it, print the reply
void Client::run() {
    while (true) {
        logLine(">> ", false);
        string line;
        // getline() fails at end of input (Ctrl-D or closed pipe).
        // The old code ignored that and looped forever at 100% CPU.
        if (!getline(cin, line)) break;

        vector<string> args = splitWords(line);
        if (args.empty()) continue;
        if (args[0] == "quit") break;
        logLine(executeCommand(args, line));
    }
}

string Client::login(const vector<string>& args, const string& line) {
    if (!currentUser.empty()) return "ERROR: Already logged in as " + currentUser;
    if (args.size() != 3) return "ERROR: Usage: login <user_id> <password>";

    // We add our peer address, so the tracker can tell downloaders where to find us
    string command = line + " " + self.ip + " " + to_string(self.port);
    string reply = tracker.request(command);
    if (isSuccess(reply)) {
        currentUser = args[1];
        tracker.saveLogin(command);   // replayed automatically after a tracker failover
    }
    return reply;
}

string Client::executeCommand(const vector<string>& args, const string& line) {
    string cmd = args[0];

    if (cmd == "login") return login(args, line);

    if (cmd == "logout") {
        // Always end the local session, even if the tracker had already forgotten it
        string reply = tracker.request("logout");
        currentUser = "";
        tracker.forgetLogin();
        return reply;
    }

    if (cmd == "show_downloads") return downloads.showDownloads();

    // File commands have client-side work (hashing, transfers) -> need login here
    bool isFileCommand = (cmd == "upload_file" || cmd == "download_file" || cmd == "stop_share");
    if (isFileCommand && currentUser.empty()) return "ERROR: Please login first";

    if (cmd == "upload_file") {
        if (args.size() != 3) return "ERROR: Usage: upload_file <group_id> <file_path>";
        return uploads.upload(args[1], args[2]);
    }
    if (cmd == "download_file") {
        if (args.size() != 4) return "ERROR: Usage: download_file <group_id> <file_name> <destination_path>";
        return downloads.startDownload(args[1], args[2], args[3]);
    }
    if (cmd == "stop_share") {
        if (args.size() != 3) return "ERROR: Usage: stop_share <group_id> <file_name>";
        return uploads.stopShare(args[1], args[2]);
    }

    // All other commands (create_user, create_group, join_group, leave_group,
    // list_groups, list_requests, accept_request, list_files) are handled
    // completely by the tracker - it checks arguments and login itself.
    return tracker.request(line);
}
