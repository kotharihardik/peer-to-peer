// DistributedIndex.cpp - the local part of the distributed metadata store
#include "DistributedIndex.h"
#include "Hashing.h"
#include "Protocol.h"
#include "Utils.h"

#include <ctime>

using namespace std;

string DistributedIndex::buildPut(unsigned int key, const FileMetadata& meta, unsigned int peerId,
                                  const string& ip, int transferPort) {
    string out = "PUT " + to_string(key) + " " + meta.name + " " + to_string(meta.size) + " " + meta.sha1 + " " +
                 to_string(meta.pieceHashes.size());
    for (size_t i = 0; i < meta.pieceHashes.size(); i++) out += " " + meta.pieceHashes[i];
    out += " " + to_string(peerId) + " " + ip + " " + to_string(transferPort);
    return out;
}

// PUT <key> <name> <size> <sha1> <n> <h1..hn> <peerId> <peerIp> <transferPort>
// word index:  0    1     2      3      4   5      5+n      6+n      7+n
// so a valid line always has exactly 8 + n words.
bool DistributedIndex::applyPut(const vector<string>& w) {
    // A valid line always has exactly 9 + pieceCount words.
    long long key = 0, size = 0, count = 0, peerId = 0, port = 0;
    if (w.size() < 9 || w[0] != "PUT") return false;
    if (!parseInt(w[1], key) || !parseInt(w[3], size) || !parseInt(w[5], count)) return false;
    if (key < 0 || key >= (long long)ID_SPACE || size < 0 || count < 0) return false;
    if ((long long)w.size() != 9 + count) return false;

    // Never trust a remote node: check the metadata exactly as module 1's
    // tracker did, so one bad message cannot poison the index.
    if (!isValidName(w[2]) || !isSha1Hex(w[4])) return false;
    if (count != getPieceCount(size)) return false;

    FileMetadata meta;
    meta.name = w[2];
    meta.size = size;
    meta.sha1 = w[4];
    for (long long i = 0; i < count; i++) {
        const string& hash = w[(size_t)(6 + i)];   // hashes occupy 6 .. 5+count
        if (!isSha1Hex(hash)) return false;
        meta.pieceHashes.push_back(hash);
    }

    if (!parseInt(w[(size_t)(6 + count)], peerId) || !parseInt(w[(size_t)(8 + count)], port)) return false;
    if (peerId < 0 || peerId >= (long long)ID_SPACE || port <= 0 || port > 65535) return false;

    IndexPeer peer;
    peer.nodeId = (unsigned int)peerId;
    peer.ip = w[(size_t)(7 + count)];
    peer.transferPort = (int)port;
    peer.lastSeen = (long long)time(NULL);

    lock_guard<mutex> lock(mtx);
    map<string, IndexEntry>::iterator found = entries.find(meta.name);
    if (found == entries.end()) {
        // First peer to publish this name decides the file's metadata
        IndexEntry fresh;
        fresh.key = (unsigned int)key;
        fresh.meta = meta;
        fresh.peers[peer.nodeId] = peer;
        entries[meta.name] = fresh;
        return true;
    }
    // A second peer must be offering the SAME file, otherwise a downloader
    // could end up mixing pieces of two different files under one name.
    if (found->second.meta.sha1 != meta.sha1) return false;
    found->second.peers[peer.nodeId] = peer;   // insert or refresh the timestamp
    return true;
}

int DistributedIndex::applyBulk(const string& text) {
    int stored = 0;
    vector<string> lines = splitLines(text);
    for (size_t i = 0; i < lines.size(); i++) {
        if (applyPut(splitWords(lines[i]))) stored++;
    }
    return stored;
}

bool DistributedIndex::fetch(const string& fileName, IndexEntry& out) {
    lock_guard<mutex> lock(mtx);
    if (!entries.count(fileName)) return false;
    out = entries[fileName];
    return !out.peers.empty();
}

void DistributedIndex::removePeer(const string& fileName, unsigned int peerId) {
    lock_guard<mutex> lock(mtx);
    if (!entries.count(fileName)) return;
    entries[fileName].peers.erase(peerId);
    if (entries[fileName].peers.empty()) entries.erase(fileName);
}

int DistributedIndex::expireOld() {
    lock_guard<mutex> lock(mtx);
    long long now = (long long)time(NULL);
    int removed = 0;

    map<string, IndexEntry>::iterator it = entries.begin();
    while (it != entries.end()) {
        map<unsigned int, IndexPeer>::iterator p = it->second.peers.begin();
        while (p != it->second.peers.end()) {
            if (now - p->second.lastSeen > ENTRY_TTL_SEC) {
                p = it->second.peers.erase(p);
                removed++;
            } else {
                p++;
            }
        }
        if (it->second.peers.empty()) it = entries.erase(it);
        else it++;
    }
    return removed;
}

string DistributedIndex::exportRange(unsigned int low, unsigned int high) {
    return exportSelected(low, high, true);
}

string DistributedIndex::exportComplement(unsigned int low, unsigned int high) {
    return exportSelected(low, high, false);
}

// One PUT line per (file, peer) pair. A file usually has few seeders, so using
// the same format as a normal publish keeps the code simple.
string DistributedIndex::exportSelected(unsigned int low, unsigned int high, bool wantInside) {
    lock_guard<mutex> lock(mtx);
    string out;
    for (auto& e : entries) {
        bool inside = inRangeExclIncl(e.second.key, low, high);
        if (inside != wantInside) continue;
        for (auto& p : e.second.peers) {
            out += buildPut(e.second.key, e.second.meta, p.second.nodeId, p.second.ip, p.second.transferPort);
            out += "\n";
        }
    }
    if (!out.empty()) out.pop_back();
    return out;
}

string DistributedIndex::describe() {
    lock_guard<mutex> lock(mtx);
    if (entries.empty()) return "This node stores no metadata";

    string out = "Metadata stored on this node:";
    for (auto& e : entries) {
        out += "\n  key " + to_string(e.second.key) + "  " + e.first + "  (" + to_string(e.second.meta.size) +
               " bytes, " + to_string(e.second.meta.pieceHashes.size()) + " pieces)";
        for (auto& p : e.second.peers) {
            out += "\n      seeder: node " + to_string(p.second.nodeId) + " at " + p.second.ip + ":" +
                   to_string(p.second.transferPort);
        }
    }
    return out;
}

int DistributedIndex::countFiles() {
    lock_guard<mutex> lock(mtx);
    return (int)entries.size();
}
