// sync.cpp
#include "header.h"
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <ctime>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace std;

// ---------------- EXTERNAL GLOBALS ----------------
extern unordered_map<string, User> users;
extern unordered_map<string, Group> groups;
extern unordered_map<string, unordered_map<string, FileMeta>> groupFiles;
extern vector<pair<string,int>> trackersInfo;  // <IP, port>
extern int myTrackerNo;



extern mutex user_mutex;
extern mutex group_mutex;
extern mutex metadataMutex;

// ---------------- UTILS ----------------

// Send a message to a tracker at ip:syncPort
bool sendToTracker(const string &ip, int syncPort, const string &msg) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(syncPort);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());

    if (connect(s, (sockaddr*)&addr, sizeof(addr)) != 0) {
        close(s);
        return false;
    }

    ssize_t w = send(s, msg.c_str(), msg.size(), 0);
    close(s);
    return w == (ssize_t)msg.size();
}

// Broadcast a client-origin command line to all other trackers
void broadcastCommandToTrackers(const string &cmd_line) {
    if (g_is_sync) return; // avoid loops
    string msg = cmd_line;
    if (msg.empty() || msg.back() != '\n') msg.push_back('\n');

    for (size_t i = 0; i < trackersInfo.size(); ++i) {
        int peerNo = (int)i + 1;
        if (peerNo == myTrackerNo) continue;

        const auto &tp = trackersInfo[i];
        const string &peerIP = tp.first;
        int syncPort = tp.second + 1000;

        if (!sendToTracker(peerIP, syncPort, msg)) {
            cerr << "[SYNC] Peer " << peerIP << ":" << syncPort << " unreachable\n";
        }
    }
}

// ---------------- SERIALIZATION ----------------

// Serialize full state (must be called under locks)
string serializeFullStateLocked() {
    ostringstream oss;
    oss << "FULLSTATE_BEGIN\n";

    // USERS
    for (auto &p : users) {
        const User &u = p.second;
        oss << "USER|" << u.userId << "|" << u.password << "|"
            << (u.loggedIn ? "1" : "0") << "|" << u.ip << "|" << u.port << "\n";
    }

    // GROUPS
    for (auto &p : groups) {
        const Group &g = p.second;
        oss << "GROUP|" << g.groupId << "|" << g.owner << "|";
        bool first = true;
        for (auto &m : g.members) {
            if (!first) oss << ",";
            oss << m;
            first = false;
        }
        oss << "|";
        first = true;
        for (auto &r : g.pendingRequests) {
            if (!first) oss << ",";
            oss << r;
            first = false;
        }
        oss << "\n";
    }

    // FILES
    for (auto &gentry : groupFiles) {
        const string &gid = gentry.first;
        for (auto &fentry : gentry.second) {
            const FileMeta &fm = fentry.second;
            oss << "FILE|" << gid << "|" << fm.filename << "|" << fm.filesize
                << "|" << fm.fileSha1 << "|" << fm.pieceCount << "|";

            // piece hashes
            for (size_t i = 0; i < fm.pieceSha1.size(); ++i) {
                if (i) oss << ",";
                oss << fm.pieceSha1[i];
            }
            oss << "|";

            // seeders: uid:idx1,idx2;uid2:...
            bool firstSeeder = true;
            for (auto &s : fm.seeders) {
                if (!firstSeeder) oss << ";";
                oss << s.first << ":";
                bool firstPiece = true;
                for (int idx : s.second.pieces) {
                    if (!firstPiece) oss << ",";
                    oss << idx;
                    firstPiece = false;
                }
                firstSeeder = false;
            }
            oss << "\n";
        }
    }

    oss << "FULLSTATE_END\n";
    return oss.str();
}

// Serialize full state with locking
string serializeFullState() {
    lock_guard<mutex> ul(user_mutex);
    lock_guard<mutex> gl(group_mutex);
    lock_guard<mutex> ml(metadataMutex);
    return serializeFullStateLocked();
}

// ---------------- APPLY COMMANDS ----------------

// Apply a single command from a remote tracker
void applyRemoteCommand(const string &cmd_line, const string &peerIP, int peerPort) {
    g_is_sync = true;
    try {
        string res = handleCommand(cmd_line, peerIP, peerPort, -1);
        (void)res;
    } catch (...) {}
    g_is_sync = false;
}

// Apply a full-state payload
void applyFullStatePayload(const string &payload) {
    stringstream ss(payload);
    string line;

    unordered_map<string, User> newUsers;
    unordered_map<string, Group> newGroups;
    unordered_map<string, unordered_map<string, FileMeta>> newGroupFiles;

    while (getline(ss, line)) {
        if (line.empty() || line == "FULLSTATE_BEGIN" || line == "FULLSTATE_END") continue;

        vector<string> parts;
        string tmp;
        stringstream ls(line);
        while (getline(ls, tmp, '|')) parts.push_back(tmp);
        if (parts.empty()) continue;

        if (parts[0] == "USER") {
            if (parts.size() < 6) continue;
            User u;
            u.userId = parts[1];
            u.password = parts[2];
            u.loggedIn = (parts[3] == "1");
            u.ip = parts[4];
            u.port = stoi(parts[5]);
            u.socketFd = -1;
            newUsers[u.userId] = u;
        } else if (parts[0] == "GROUP") {
            if (parts.size() < 5) continue;
            Group g;
            g.groupId = parts[1];
            g.owner = parts[2];
            string membersCSV = parts[3];
            string pendingCSV = parts[4];

            if (!membersCSV.empty()) {
                stringstream ms(membersCSV);
                string m;
                while (getline(ms, m, ',')) if (!m.empty()) g.members.insert(m);
            }
            if (!pendingCSV.empty()) {
                stringstream ps(pendingCSV);
                string r;
                while (getline(ps, r, ',')) if (!r.empty()) g.pendingRequests.insert(r);
            }
            newGroups[g.groupId] = g;
        } else if (parts[0] == "FILE") {
            if (parts.size() < 8) continue;
            string gid = parts[1];
            FileMeta fm;
            fm.filename = parts[2];
            fm.filesize = stoll(parts[3]);
            fm.fileSha1 = parts[4];
            fm.pieceCount = stoi(parts[5]);

            string piecesCSV = parts[6];
            if (!piecesCSV.empty()) {
                stringstream ps(piecesCSV);
                string t;
                while (getline(ps, t, ',')) if (!t.empty()) fm.pieceSha1.push_back(t);
            }

            string seederspec = parts[7];
            if (!seederspec.empty()) {
                stringstream ss2(seederspec);
                string se;
                while (getline(ss2, se, ';')) {
                    auto pos = se.find(':');
                    if (pos == string::npos) continue;
                    string suid = se.substr(0, pos);
                    string idxs = se.substr(pos + 1);
                    stringstream is(idxs);
                    string id;
                    while (getline(is, id, ',')) if (!id.empty()) fm.seeders[suid].pieces.insert(stoi(id));
                }
            }
            newGroupFiles[gid][fm.filename] = fm;
        }
    }

    lock_guard<mutex> ul(user_mutex);
    users = std::move(newUsers);
    lock_guard<mutex> gl(group_mutex);
    groups = std::move(newGroups);
    lock_guard<mutex> ml(metadataMutex);
    groupFiles = std::move(newGroupFiles);
}

// ---------------- SYNC SERVER ----------------

void syncListener(int mySyncPort) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("sync socket"); return; }

    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(mySyncPort);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(s, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); close(s); return; }
    if (listen(s, 10) < 0) { perror("listen"); close(s); return; }

    cerr << "[SYNC] listening on port " << mySyncPort << endl;

    while (true) {
        sockaddr_in peerAddr{};
        socklen_t plen = sizeof(peerAddr);
        int cs = accept(s, (sockaddr*)&peerAddr, &plen);
        if (cs < 0) { perror("accept"); continue; }

        thread([cs, peerAddr]() {
            char tmp[4096];
            string buffer;
            int n;
            while ((n = recv(cs, tmp, sizeof(tmp), 0)) > 0) {
                buffer.append(tmp, n);
                if (buffer.find('\n') != string::npos) break;
            }
            if (!buffer.empty() && buffer.back() == '\n') buffer.pop_back();

            if (buffer == "FULLSTATE_REQUEST") {
                string state = serializeFullState();
                send(cs, state.c_str(), state.size(), 0);
                close(cs);
                return;
            }

            if (buffer.find("FULLSTATE_BEGIN") != string::npos) {
                int n2;
                while ((n2 = recv(cs, tmp, sizeof(tmp), 0)) > 0) {
                    buffer.append(tmp, n2);
                    if (buffer.find("FULLSTATE_END") != string::npos) break;
                }
                applyFullStatePayload(buffer);
                close(cs);
                return;
            }

            char peerIPStr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &peerAddr.sin_addr, peerIPStr, sizeof(peerIPStr));
            int peerPort = ntohs(peerAddr.sin_port);
            applyRemoteCommand(buffer, string(peerIPStr), peerPort);
            close(cs);
        }).detach();
    }

    close(s);
}

// ---------------- REQUEST FULL STATE ----------------

bool requestFullStateFromPeerAndApply(const string &peerIP, int peerSyncPort) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(peerSyncPort);
    addr.sin_addr.s_addr = inet_addr(peerIP.c_str());

    bool ok = false;
    if (connect(s, (sockaddr*)&addr, sizeof(addr)) == 0) {
        string req = "FULLSTATE_REQUEST\n";
        send(s, req.c_str(), req.size(), 0);

        string buf;
        char tmp[4096];
        int n;
        while ((n = recv(s, tmp, sizeof(tmp), 0)) > 0) {
            buf.append(tmp, n);
            if (buf.find("FULLSTATE_END") != string::npos) break;
        }
        if (!buf.empty()) {
            applyFullStatePayload(buf);
            ok = true;
        }
    }
    close(s);
    return ok;
}
