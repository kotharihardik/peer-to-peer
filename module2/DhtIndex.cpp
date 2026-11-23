// DhtIndex.cpp - publish / find file metadata through the overlay
#include "DhtIndex.h"
#include "Hashing.h"

#include <chrono>

using namespace std;

DhtIndex::DhtIndex(DhtNode& n) : node(n) {
    running = false;
}

DhtIndex::~DhtIndex() {
    stop();
}

void DhtIndex::start() {
    running = true;
    announcer = thread(&DhtIndex::announceLoop, this);
}

void DhtIndex::stop() {
    if (!running) return;
    running = false;
    if (announcer.joinable()) announcer.join();
}

// ===================== routing helper =====================

NodeInfo DhtIndex::findOwner(unsigned int key, const string& what, int& hops, bool verbose) {
    vector<string> trace;
    NodeInfo owner;

    // A lookup can fail because it was routed through a node that has just
    // crashed. Each failed attempt removes that node from the routing table,
    // so a retry takes a different path. Three attempts are plenty.
    for (int attempt = 0; attempt < 3; attempt++) {
        owner = node.lookup(key, hops, trace);
        if (!owner.empty()) break;
    }

    if (verbose) {
        logLine("[LOOKUP] " + what + "  ->  key " + to_string(key));
        for (size_t i = 0; i < trace.size(); i++) logLine("[LOOKUP]   " + trace[i]);
        if (owner.empty()) logLine("[LOOKUP]   no answer from the overlay");
        else {
            logLine("[LOOKUP]   responsible node: " + owner.toString() + "   (" + to_string(hops) + " hop(s))");
        }
    }
    return owner;
}

// ===================== publishing =====================

bool DhtIndex::storeOne(const string& scope, const FileMetadata& meta, bool verbose, string& message) {
    unsigned int key = fileKey(scope, meta.name);
    int hops = 0;
    NodeInfo owner = findOwner(key, "publish " + meta.name, hops, verbose);
    if (owner.empty()) {
        message = "ERROR: the overlay did not answer, metadata was not stored";
        return false;
    }

    // The index keeps my TRANSFER address, not my DHT address: that is where
    // other peers must connect to fetch pieces.
    NodeInfo me = node.getSelf();
    Address transfer = node.getTransferAddress();
    string put = DistributedIndex::buildPut(key, meta, me.id, transfer.ip, transfer.port);

    string reply;
    if (!node.ask(owner, put, reply)) {
        message = "ERROR: responsible node " + owner.toString() + " did not answer";
        return false;
    }
    if (reply != "OK") {
        message = "ERROR: a different file is already published under the name " + meta.name;
        return false;
    }

    message = "SUCCESS: " + meta.name + " published (key " + to_string(key) + " -> node " +
              to_string(owner.id) + ", " + to_string(hops) + " hop(s))";
    return true;
}

string DhtIndex::publish(const string& scope, const FileMetadata& meta, bool& ok) {
    string message;
    ok = storeOne(scope, meta, true, message);
    if (!ok) return message;

    // Remember it so the announce thread can refresh it later
    lock_guard<mutex> lock(mtx);
    MyFile mine;
    mine.scope = scope;
    mine.meta = meta;
    myFiles[scope + "/" + meta.name] = mine;
    return message;
}

string DhtIndex::unpublish(const string& scope, const string& fileName, bool& ok) {
    ok = false;
    {
        lock_guard<mutex> lock(mtx);
        string mapKey = scope + "/" + fileName;
        if (!myFiles.count(mapKey)) return "ERROR: you are not sharing " + fileName;
        myFiles.erase(mapKey);
    }
    // Stop serving the file locally even if the overlay cannot be reached: the
    // record left in the index is soft state and expires by itself.
    ok = true;

    unsigned int key = fileKey(scope, fileName);
    int hops = 0;
    NodeInfo owner = findOwner(key, "unpublish " + fileName, hops, false);
    if (owner.empty()) return "SUCCESS: stopped sharing " + fileName + " (index entry will expire on its own)";

    string reply;
    NodeInfo me = node.getSelf();
    if (!node.ask(owner, "DROP " + fileName + " " + to_string(me.id), reply)) {
        return "SUCCESS: stopped sharing " + fileName + " (index entry will expire on its own)";
    }
    return "SUCCESS: stopped sharing " + fileName + " and removed it from node " + to_string(owner.id);
}

// Re-publish everything I share, quietly, every ANNOUNCE_SEC seconds.
void DhtIndex::announceLoop() {
    int waitedMs = 0;
    while (running) {
        this_thread::sleep_for(chrono::milliseconds(500));
        waitedMs += 500;
        if (waitedMs < ANNOUNCE_SEC * 1000) continue;
        waitedMs = 0;

        // Copy the list first, so the overlay calls below are made without
        // holding the mutex (they do network I/O and may block).
        vector<MyFile> list;
        {
            lock_guard<mutex> lock(mtx);
            for (auto& f : myFiles) list.push_back(f.second);
        }
        for (size_t i = 0; i < list.size() && running; i++) {
            string message;
            storeOne(list[i].scope, list[i].meta, false, message);
        }
    }
}

// ===================== looking up =====================

bool DhtIndex::lookup(const string& scope, const string& fileName, FileMetadata& meta, vector<Seeder>& seeders,
                      string& errorMsg) {
    unsigned int key = fileKey(scope, fileName);
    int hops = 0;

    chrono::steady_clock::time_point started = chrono::steady_clock::now();
    NodeInfo owner = findOwner(key, "search " + fileName, hops, true);
    if (owner.empty()) {
        errorMsg = "ERROR: the overlay did not answer the lookup";
        return false;
    }

    string reply;
    if (!node.ask(owner, "GET " + fileName, reply)) {
        errorMsg = "ERROR: responsible node " + owner.toString() + " did not answer";
        return false;
    }
    long long elapsedMs =
        chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - started).count();
    node.recordLookup(hops, elapsedMs);

    if (reply == "NOTFOUND") {
        errorMsg = "ERROR: " + fileName + " is not in the index (node " + to_string(owner.id) +
                   " is responsible for key " + to_string(key) + ")";
        return false;
    }

    // FOUND <size> <sha1> <pieceCount>
    // <piece hash>            (one line each)
    // PEER <nodeId> <ip> <transferPort>
    vector<string> lines = splitLines(reply);
    vector<string> first;
    if (!lines.empty()) first = splitWords(lines[0]);

    long long size = 0, count = 0;
    bool valid = first.size() == 4 && first[0] == "FOUND" && parseInt(first[1], size) &&
                 parseInt(first[3], count) && size >= 0 && count >= 0 &&
                 (long long)lines.size() >= 1 + count;
    if (!valid) {
        errorMsg = "ERROR: malformed reply from node " + owner.toString();
        return false;
    }

    meta.name = fileName;
    meta.size = size;
    meta.sha1 = first[2];
    for (long long i = 1; i <= count; i++) meta.pieceHashes.push_back(lines[(size_t)i]);

    for (size_t i = (size_t)(1 + count); i < lines.size(); i++) {
        vector<string> w = splitWords(lines[i]);
        long long port = 0;
        if (w.size() != 4 || w[0] != "PEER" || !parseInt(w[3], port)) continue;

        Seeder s;
        s.peerId = "node" + w[1];
        s.address = Address(w[2], (int)port);
        seeders.push_back(s);
    }
    if (seeders.empty()) {
        errorMsg = "ERROR: " + fileName + " has no seeder in the index";
        return false;
    }
    return true;
}

// The "search" command: same lookup as a download, but only reports
string DhtIndex::search(const string& scope, const string& fileName) {
    FileMetadata meta;
    vector<Seeder> seeders;
    string errorMsg;
    if (!lookup(scope, fileName, meta, seeders, errorMsg)) return errorMsg;

    string out = "Found " + meta.name + ": " + to_string(meta.size) + " bytes, " +
                 to_string(meta.pieceHashes.size()) + " piece(s), sha1 " + meta.sha1.substr(0, 12) + "...";
    out += "\nSeeders holding this file:";
    for (size_t i = 0; i < seeders.size(); i++) {
        out += "\n  " + seeders[i].peerId + " at " + seeders[i].address.toString();
    }
    return out;
}
