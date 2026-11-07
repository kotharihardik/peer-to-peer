#pragma once
// =====================================================================
// TrackerState - ALL data of a tracker + the rules to change it
// =====================================================================
// Stores users, groups and shared-file metadata. No networking in here,
// only "business logic", so it is easy to read and test.
//
// Thread safety: one mutex protects all data (coarse-grained locking).
// Operations are short map lookups and no I/O is done while holding the lock;
// a single lock also rules out deadlocks.
//
// SYNC OPS: every successful change is also written as a small text line
// ("sync op") that is sent to the other tracker:
//     USER    <uid> <password>          ONLINE  <uid> <peerIp> <peerPort>
//     OFFLINE <uid>                     GROUP   <gid> <owner>
//     REQUEST <gid> <uid>               MEMBER  <gid> <uid>
//     LEAVE   <gid> <uid>               SEED    <gid> <file> <uid>
//     UNSEED  <gid> <file> <uid>
//     FILE    <gid> <file> <size> <sha1> <pieceCount> <pieceHash1> ... <pieceHashN>
//
// Ops are idempotent: applying one twice has the same effect as once, so
// duplicates (e.g. a snapshot overlapping live ops) are harmless. A full
// snapshot is simply the list of ops that rebuilds the whole state.

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

using namespace std;

struct User {
    string password;
    bool online = false;         // logged in on ANY tracker
    bool localSession = false;   // logged in through THIS tracker (not synced)
    string peerIp;               // where other clients can download from this user
    int peerPort = 0;
};

struct SharedFile {
    long long size = 0;
    string sha1;                 // hash of the whole file
    vector<string> pieceHashes;  // one SHA1 per 512 KB piece
    set<string> seeders;         // users that have the COMPLETE file
};

struct Group {
    string owner;
    // Sorted set: iteration order is identical on both trackers, so choosing
    // a new owner (first member) gives the same result everywhere.
    set<string> members;
    set<string> pending;         // users waiting for the owner to accept
    map<string, SharedFile> files;
};

// What a client command returns: text for the client + sync ops for the other tracker
struct Reply {
    string text;
    string syncOps;              // empty = nothing changed, nothing to sync
};

class TrackerState {
public:
    // ---- user commands ----
    Reply createUser(const string& uid, const string& password);
    Reply login(const string& uid, const string& password, const string& peerIp, int peerPort);
    Reply logout(const string& uid);

    // ---- group commands ----
    Reply createGroup(const string& uid, const string& gid);
    Reply joinGroup(const string& uid, const string& gid);
    Reply leaveGroup(const string& uid, const string& gid);
    Reply listGroups();
    Reply listRequests(const string& uid, const string& gid);
    Reply acceptRequest(const string& owner, const string& gid, const string& uid);

    // ---- file commands ----
    Reply uploadFile(const string& uid, const string& gid, const string& fileName, long long size,
                     const string& sha1, const vector<string>& pieceHashes);
    Reply listFiles(const string& uid, const string& gid);
    Reply downloadInfo(const string& uid, const string& gid, const string& fileName);
    Reply stopShare(const string& uid, const string& gid, const string& fileName);

    // ---- synchronization ----
    void applyOps(const string& opLines);   // apply ops that came from the other tracker
    string snapshot();                      // whole state written as ops

private:
    // Helpers below expect that "mtx" is ALREADY locked by the caller
    bool isOnline(const string& uid);
    Group* findGroup(const string& gid);
    Group* findGroupAsMember(const string& uid, const string& gid, string& error);
    void removeMember(const string& gid, const string& uid);
    void removeSeeder(Group& group, const string& fileName, const string& uid);
    void applyOneOp(const vector<string>& words);
    string makeFileOp(const string& gid, const string& fileName, const SharedFile& file);

    mutex mtx;
    map<string, User> users;     // key = user id
    map<string, Group> groups;   // key = group id
};
