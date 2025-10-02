#include "header.h"
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <sys/socket.h>

using namespace std;

// External globals
extern unordered_map<string, User> users;
extern unordered_map<string, Group> groups;
extern unordered_map<string, unordered_map<string, FileMeta>> groupFiles;

// Helper: send entire string to socket
bool sendAll(int sock, const string &data) {
    size_t totalSent = 0;
    while (totalSent < data.size()) {
        ssize_t n = send(sock, data.c_str() + totalSent, data.size() - totalSent, 0);
        if (n <= 0) {
            cerr << "[TRACKER] send() error after " << totalSent << " bytes\n";
            return false;
        }
        totalSent += n;
        cerr << "[TRACKER] Sent " << n << " bytes, total so far = " << totalSent << endl;
    }
    cerr << "[TRACKER] Finished sending " << totalSent << " bytes\n";
    return true;
}

// Handle download_file command directly
void handleDownloadCommand(int clientSock, const string &cmd) {
    stringstream ss(cmd);
    string command, groupId, filename;
    ss >> command >> groupId >> filename;

    if (command != "download_file") {
        sendAll(clientSock, "ERROR Unknown command\n");
        return;  // Don't close here
    }

    if (groupFiles.find(groupId) == groupFiles.end() ||
        groupFiles[groupId].find(filename) == groupFiles[groupId].end()) {
        sendAll(clientSock, "ERROR File not found\n");
        return;  // Don't close here
    }

    FileMeta &fmeta = groupFiles[groupId][filename];

    cerr << "[TRACKER] Preparing response for file: " << filename << endl;
    cerr << "[TRACKER] Pieces: " << fmeta.pieceCount << endl;

    ostringstream resp;
    resp << fmeta.filesize << " " << fmeta.pieceCount << " " << fmeta.fileSha1 << "\n";

    for (const string &ph : fmeta.pieceSha1) {
        resp << ph << " ";
    }
    resp << "\n";

    for (const auto &[userId, seeder] : fmeta.seeders) {
        if (users.find(userId) == users.end()) continue;
        const User &u = users[userId];

        resp << u.ip << ":" << u.port << ":" << seeder.filepath << ":";
        int count = 0;
        for (int p : seeder.pieces) {
            if (count++) resp << ",";
            resp << p;
        }
        resp << " ";
    }
    resp << "\n";

    string response = resp.str();
    cerr << "[TRACKER] Response size = " << response.size() << " bytes" << endl;

    if (!sendAll(clientSock, response)) {
        cerr << "[TRACKER] Failed to send download response\n";
    }

    close(clientSock);
}