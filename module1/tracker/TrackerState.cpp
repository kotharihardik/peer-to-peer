// TrackerState.cpp - business rules of the tracker (users, groups, files, sync)
#include "TrackerState.h"
#include "Protocol.h"
#include "Utils.h"

#include <sstream>

using namespace std;

// Small helpers to build replies
static Reply successReply(const string& text, const string& ops = "") {
    Reply r;
    r.text = "SUCCESS: " + text;
    r.syncOps = ops;
    return r;
}

static Reply errorReply(const string& text) {
    Reply r;
    r.text = "ERROR: " + text;
    return r;
}

static Reply infoReply(const string& text) {   // plain data reply (lists etc.)
    Reply r;
    r.text = text;
    return r;
}

// ======================= helper functions (mutex already held) =======================

bool TrackerState::isOnline(const string& uid) {
    return users.count(uid) && users[uid].online;
}

// Returns a pointer into the map, or NULL if the group does not exist
Group* TrackerState::findGroup(const string& gid) {
    if (!groups.count(gid)) return NULL;
    return &groups[gid];
}

// Most file commands need "group exists" AND "user is a member" -> one helper
Group* TrackerState::findGroupAsMember(const string& uid, const string& gid, string& errorMsg) {
    Group* group = findGroup(gid);
    if (group == NULL) {
        errorMsg = "Group does not exist";
        return NULL;
    }
    if (!group->members.count(uid)) {
        errorMsg = "You are not a member of this group";
        return NULL;
    }
    return group;
}

// Remove a user from a file's seeder list. If nobody shares the file any more,
// the file disappears from the group.
void TrackerState::removeSeeder(Group& group, const string& fileName, const string& uid) {
    if (!group.files.count(fileName)) return;
    group.files[fileName].seeders.erase(uid);
    if (group.files[fileName].seeders.empty()) group.files.erase(fileName);
}

// A user leaves a group:
//  1. stop sharing all his files in that group
//  2. if he was the owner -> the first member (sorted) becomes the new owner
//  3. if no members are left -> delete the group
void TrackerState::removeMember(const string& gid, const string& uid) {
    Group* group = findGroup(gid);
    if (group == NULL || !group->members.count(uid)) return;
    group->members.erase(uid);

    vector<string> fileNames;
    for (auto& f : group->files) fileNames.push_back(f.first);
    for (auto& name : fileNames) removeSeeder(*group, name, uid);

    if (group->members.empty()) {
        groups.erase(gid);
    } else if (group->owner == uid) {
        group->owner = *group->members.begin();   // same choice on both trackers
    }
}

// Builds the "FILE ..." sync op line for one file
string TrackerState::makeFileOp(const string& gid, const string& fileName, const SharedFile& file) {
    stringstream op;
    op << "FILE " << gid << " " << fileName << " " << file.size << " " << file.sha1 << " "
       << file.pieceHashes.size();
    for (auto& h : file.pieceHashes) op << " " << h;
    return op.str();
}

// ======================= user commands =======================

Reply TrackerState::createUser(const string& uid, const string& password) {
    if (!isValidName(uid) || !isValidName(password)) return errorReply("Invalid user id or password");
    lock_guard<mutex> lock(mtx);
    if (users.count(uid)) return errorReply("User already exists");
    users[uid].password = password;
    return successReply("User created", "USER " + uid + " " + password);
}

Reply TrackerState::login(const string& uid, const string& password, const string& peerIp, int peerPort) {
    lock_guard<mutex> lock(mtx);
    if (!users.count(uid)) return errorReply("User does not exist");
    User& user = users[uid];
    if (user.password != password) return errorReply("Wrong password");

    // Only a session on THIS tracker blocks login. A user marked online by the
    // other tracker may be a client failing over after that tracker crashed.
    if (user.localSession) return errorReply("User already logged in");

    user.online = true;
    user.localSession = true;
    user.peerIp = peerIp;     // other clients will download pieces from here
    user.peerPort = peerPort;
    return successReply("Logged in", "ONLINE " + uid + " " + peerIp + " " + to_string(peerPort));
}

// Seeder entries are kept on logout; downloadInfo() only returns online
// seeders, so a logged-out user is not offered to downloaders.
Reply TrackerState::logout(const string& uid) {
    lock_guard<mutex> lock(mtx);
    if (!users.count(uid) || !users[uid].localSession) return errorReply("User not logged in");
    users[uid].online = false;
    users[uid].localSession = false;
    return successReply("Logged out", "OFFLINE " + uid);
}

// ======================= group commands =======================

Reply TrackerState::createGroup(const string& uid, const string& gid) {
    if (!isValidName(gid)) return errorReply("Invalid group id");
    lock_guard<mutex> lock(mtx);
    if (groups.count(gid)) return errorReply("Group already exists");
    groups[gid].owner = uid;
    groups[gid].members.insert(uid);
    return successReply("Group created", "GROUP " + gid + " " + uid);
}

Reply TrackerState::joinGroup(const string& uid, const string& gid) {
    lock_guard<mutex> lock(mtx);
    Group* group = findGroup(gid);
    if (group == NULL) return errorReply("Group does not exist");
    if (group->members.count(uid)) return errorReply("Already a member");
    if (group->pending.count(uid)) return errorReply("Request already sent");
    group->pending.insert(uid);
    return successReply("Join request sent", "REQUEST " + gid + " " + uid);
}

Reply TrackerState::leaveGroup(const string& uid, const string& gid) {
    lock_guard<mutex> lock(mtx);
    Group* group = findGroup(gid);
    if (group == NULL) return errorReply("Group does not exist");
    if (!group->members.count(uid)) return errorReply("Not a member");
    removeMember(gid, uid);
    return successReply("Left group " + gid, "LEAVE " + gid + " " + uid);
}

Reply TrackerState::listGroups() {
    lock_guard<mutex> lock(mtx);
    if (groups.empty()) return infoReply("No groups available");
    string result;
    for (auto& g : groups) result += g.first + "\n";
    result.pop_back();   // remove last '\n'
    return infoReply(result);
}

Reply TrackerState::listRequests(const string& uid, const string& gid) {
    lock_guard<mutex> lock(mtx);
    Group* group = findGroup(gid);
    if (group == NULL) return errorReply("Group does not exist");
    if (group->owner != uid) return errorReply("Only the group owner can list requests");
    if (group->pending.empty()) return infoReply("No pending requests");
    string result;
    for (auto& name : group->pending) result += name + "\n";
    result.pop_back();
    return infoReply(result);
}

Reply TrackerState::acceptRequest(const string& owner, const string& gid, const string& uid) {
    lock_guard<mutex> lock(mtx);
    Group* group = findGroup(gid);
    if (group == NULL) return errorReply("Group does not exist");
    if (group->owner != owner) return errorReply("Only the group owner can accept requests");
    if (!group->pending.count(uid)) return errorReply("No such pending request");
    group->pending.erase(uid);
    group->members.insert(uid);
    return successReply("Request accepted", "MEMBER " + gid + " " + uid);
}

// ======================= file commands =======================

// The tracker stores only METADATA (size + hashes + who has it), never file data.
Reply TrackerState::uploadFile(const string& uid, const string& gid, const string& fileName, long long size,
                               const string& sha1, const vector<string>& pieceHashes) {
    // Never trust the client: validate everything before touching the state
    if (!isValidName(fileName)) return errorReply("Invalid file name");
    if (size < 0 || !isSha1Hex(sha1)) return errorReply("Invalid file metadata");
    if ((long long)pieceHashes.size() != getPieceCount(size)) return errorReply("Piece count does not match file size");
    for (auto& h : pieceHashes) {
        if (!isSha1Hex(h)) return errorReply("Invalid piece hash");
    }

    lock_guard<mutex> lock(mtx);
    string errorMsg;
    Group* group = findGroupAsMember(uid, gid, errorMsg);
    if (group == NULL) return errorReply(errorMsg);

    string ops;
    if (!group->files.count(fileName)) {
        // First person to share this file -> store its metadata
        SharedFile& file = group->files[fileName];
        file.size = size;
        file.sha1 = sha1;
        file.pieceHashes = pieceHashes;
        ops = makeFileOp(gid, fileName, file) + "\n";
    } else if (group->files[fileName].sha1 != sha1) {
        // Same name, different content (whole-file SHA1 differs): reject, so
        // pieces of two different files are never mixed.
        return errorReply("A different file with this name is already shared in the group");
    }
    group->files[fileName].seeders.insert(uid);   // same file -> one more seeder
    return successReply("File shared with group", ops + "SEED " + gid + " " + fileName + " " + uid);
}

Reply TrackerState::listFiles(const string& uid, const string& gid) {
    lock_guard<mutex> lock(mtx);
    string errorMsg;
    Group* group = findGroupAsMember(uid, gid, errorMsg);
    if (group == NULL) return errorReply(errorMsg);
    if (group->files.empty()) return infoReply("No files in this group");

    stringstream out;
    out << "Files in group " << gid << ":";
    for (auto& f : group->files) {
        int onlineSeeders = 0;
        for (auto& s : f.second.seeders) {
            if (isOnline(s)) onlineSeeders++;
        }
        out << "\n  " << f.first << " (" << f.second.size << " bytes, " << onlineSeeders
            << " seeder(s) online)";
    }
    return infoReply(out.str());
}

// Everything a client needs to download: all hashes + addresses of online seeders.
// Reply format:  "OK <size> <pieceCount> <sha1>"
//                one piece hash per line
//                one "<uid> <ip> <port>" line per online seeder
Reply TrackerState::downloadInfo(const string& uid, const string& gid, const string& fileName) {
    lock_guard<mutex> lock(mtx);
    string errorMsg;
    Group* group = findGroupAsMember(uid, gid, errorMsg);
    if (group == NULL) return errorReply(errorMsg);
    if (!group->files.count(fileName)) return errorReply("File not found in group");
    SharedFile& file = group->files[fileName];

    stringstream out;
    out << "OK " << file.size << " " << file.pieceHashes.size() << " " << file.sha1 << "\n";
    for (auto& h : file.pieceHashes) out << h << "\n";

    int seederCount = 0;
    for (auto& s : file.seeders) {
        if (!isOnline(s)) continue;   // offline users cannot send pieces
        out << s << " " << users[s].peerIp << " " << users[s].peerPort << "\n";
        seederCount++;
    }
    if (seederCount == 0) return errorReply("No seeders online for this file");
    return infoReply(out.str());
}

Reply TrackerState::stopShare(const string& uid, const string& gid, const string& fileName) {
    lock_guard<mutex> lock(mtx);
    string errorMsg;
    Group* group = findGroupAsMember(uid, gid, errorMsg);
    if (group == NULL) return errorReply(errorMsg);
    if (!group->files.count(fileName)) return errorReply("File not found in group");
    if (!group->files[fileName].seeders.count(uid)) return errorReply("You are not sharing this file");
    removeSeeder(*group, fileName, uid);
    return successReply("Stopped sharing " + fileName, "UNSEED " + gid + " " + fileName + " " + uid);
}

// ======================= synchronization =======================

// Apply ops received from the other tracker (one op per line)
void TrackerState::applyOps(const string& opLines) {
    lock_guard<mutex> lock(mtx);
    for (auto& line : splitLines(opLines)) {
        vector<string> words = splitWords(line);
        if (!words.empty()) applyOneOp(words);
    }
}

// Apply ONE op. Rules: never fail, silently ignore bad or out-of-date ops,
// and make every op idempotent (safe to apply twice).
void TrackerState::applyOneOp(const vector<string>& w) {
    string op = w[0];
    long long num = 0;

    if (op == "USER" && w.size() == 3) {
        if (!users.count(w[1])) users[w[1]].password = w[2];
    }
    else if (op == "ONLINE" && w.size() == 4 && users.count(w[1]) && parseInt(w[3], num)) {
        users[w[1]].online = true;
        users[w[1]].peerIp = w[2];
        users[w[1]].peerPort = (int)num;
    }
    else if (op == "OFFLINE" && w.size() == 2 && users.count(w[1])) {
        // Ignore a late OFFLINE if the user already has a live session here
        // (client failed over before the old tracker reported the disconnect).
        if (!users[w[1]].localSession) users[w[1]].online = false;
    }
    else if (op == "GROUP" && w.size() == 3) {
        if (!groups.count(w[1])) {
            groups[w[1]].owner = w[2];
            groups[w[1]].members.insert(w[2]);
        }
    }
    else if (op == "REQUEST" && w.size() == 3) {
        Group* group = findGroup(w[1]);
        if (group != NULL && !group->members.count(w[2])) group->pending.insert(w[2]);
    }
    else if (op == "MEMBER" && w.size() == 3) {
        Group* group = findGroup(w[1]);
        if (group != NULL) {
            group->pending.erase(w[2]);
            group->members.insert(w[2]);
        }
    }
    else if (op == "LEAVE" && w.size() == 3) {
        removeMember(w[1], w[2]);
    }
    else if (op == "FILE" && w.size() >= 6 && parseInt(w[3], num) && num >= 0) {
        // FILE <gid> <file> <size> <sha1> <count> <hash1..hashN>
        Group* group = findGroup(w[1]);
        long long count = 0;
        bool countOk = parseInt(w[5], count) && (long long)w.size() == 6 + count;
        if (group != NULL && !group->files.count(w[2]) && countOk) {
            SharedFile& file = group->files[w[2]];
            file.size = num;
            file.sha1 = w[4];
            for (size_t i = 6; i < w.size(); i++) file.pieceHashes.push_back(w[i]);
        }
    }
    else if (op == "SEED" && w.size() == 4) {
        Group* group = findGroup(w[1]);
        if (group != NULL && group->files.count(w[2])) group->files[w[2]].seeders.insert(w[3]);
    }
    else if (op == "UNSEED" && w.size() == 4) {
        Group* group = findGroup(w[1]);
        if (group != NULL) removeSeeder(*group, w[2], w[3]);
    }
}

// Whole state as a list of ops. Applying it to an empty tracker rebuilds everything.
string TrackerState::snapshot() {
    lock_guard<mutex> lock(mtx);
    stringstream out;
    for (auto& u : users) {
        out << "USER " << u.first << " " << u.second.password << "\n";
        // Only users logged in through THIS tracker are sent as online:
        // this tracker knows for sure they are connected. This stops stale
        // "online" flags (left over from a crashed tracker) from spreading.
        if (u.second.localSession) {
            out << "ONLINE " << u.first << " " << u.second.peerIp << " " << u.second.peerPort << "\n";
        }
    }
    for (auto& g : groups) {
        string gid = g.first;
        out << "GROUP " << gid << " " << g.second.owner << "\n";
        for (auto& m : g.second.members) out << "MEMBER " << gid << " " << m << "\n";
        for (auto& p : g.second.pending) out << "REQUEST " << gid << " " << p << "\n";
        for (auto& f : g.second.files) {
            out << makeFileOp(gid, f.first, f.second) << "\n";
            for (auto& s : f.second.seeders) out << "SEED " << gid << " " << f.first << " " << s << "\n";
        }
    }
    return out.str();
}
