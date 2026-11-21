#pragma once
// =====================================================================
// DistributedIndex - the slice of the global index stored on THIS node
// =====================================================================
// In module 1 one tracker held the whole index. Here every node holds a small
// part of it: the files whose key falls in (predecessor, me]. Put together,
// the nodes hold exactly what the tracker used to hold, with no single owner.
//
// What is stored is only METADATA - size, hashes and WHO has the file. The
// file bytes never come here; they stay on the peers that shared them and are
// transferred directly. So the node responsible for a file's key usually does
// not have the file at all.
//
// SOFT STATE: every peer entry carries a timestamp and is dropped if it is not
// refreshed within ENTRY_TTL_SEC. Publishers re-announce on a timer, so:
//   - a peer that crashes disappears from the index by itself
//   - copies left on nodes that are no longer responsible expire on their own
// This is the same "announce interval" idea a BitTorrent tracker uses.
//
// The local map is keyed by FILE NAME, not by the numeric key: with 16-bit ids
// two different names can hash to the same key, and both must be storable.

#include "FileMetadata.h"

#include <map>
#include <mutex>
#include <string>
#include <vector>

using namespace std;

// Drop a peer entry that has not been refreshed for this long
const long long ENTRY_TTL_SEC = 90;

// One peer that holds a file
struct IndexPeer {
    unsigned int nodeId;   // its DHT node id
    string ip;
    int transferPort;      // where its PeerServer serves pieces
    long long lastSeen;    // seconds since epoch
};

// Everything the index knows about one file
struct IndexEntry {
    unsigned int key;
    FileMetadata meta;
    map<unsigned int, IndexPeer> peers;   // keyed by node id, so re-publishing just refreshes
};

class DistributedIndex {
public:
    // The one wire format for storing: used for a normal publish, for handing
    // keys over to a new node, and for replication.
    //   PUT <key> <name> <size> <sha1> <n> <h1> .. <hn> <peerId> <peerIp> <transferPort>
    static string buildPut(unsigned int key, const FileMetadata& meta, unsigned int peerId, const string& ip,
                           int transferPort);

    // Parse and store one PUT line. Returns false if the line is malformed.
    bool applyPut(const vector<string>& words);

    // Apply many PUT lines (a handoff or a replication push)
    int applyBulk(const string& text);

    bool fetch(const string& fileName, IndexEntry& out);
    void removePeer(const string& fileName, unsigned int peerId);

    // Throw away peer entries nobody refreshed, and files left with no peers
    int expireOld();

    // PUT lines for the entries whose key IS / IS NOT inside (low, high]
    string exportRange(unsigned int low, unsigned int high);
    string exportComplement(unsigned int low, unsigned int high);

    string describe();
    int countFiles();

private:
    // DHT server threads and the maintenance thread both touch the map
    mutex mtx;
    map<string, IndexEntry> entries;

    string exportSelected(unsigned int low, unsigned int high, bool wantInside);
};
