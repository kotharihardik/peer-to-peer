#pragma once
// =====================================================================
// MetadataIndex - WHERE a file's metadata and seeder list are kept
// =====================================================================
// This is the ONE thing that differs between the two generations of this
// project, so it is kept as a tiny interface with three methods:
//
//   Module 1 (centralized)   -> TrackerIndex : asks the central tracker
//   Module 2 (decentralized) -> DhtIndex     : asks the Chord DHT
//
// Everything else - hashing, pieces, seeding, downloading - is shared code
// in common/ and does not care which of the two is plugged in.
//
// "scope" is the namespace a file name lives in:
//   module 1 -> the group id (files belong to a group)
//   module 2 -> one global scope (a DHT has no groups)

#include "FileMetadata.h"
#include "Utils.h"

#include <string>
#include <vector>

using namespace std;

// A peer that has the COMPLETE file and can serve its pieces
struct Seeder {
    string peerId;     // user id (module 1) or DHT node id (module 2)
    Address address;   // where that peer's PeerServer listens
};

class MetadataIndex {
public:
    virtual ~MetadataIndex() {}

    // "I have this file": store its metadata and add me to the seeder list.
    // Returns a message for the user; ok = the index accepted it.
    virtual string publish(const string& scope, const FileMetadata& meta, bool& ok) = 0;

    // "I stopped sharing this file": remove me from the seeder list.
    virtual string unpublish(const string& scope, const string& fileName, bool& ok) = 0;

    // "Who has this file and what are its piece hashes?"
    // Returns false and fills errorMsg when the file cannot be located.
    virtual bool lookup(const string& scope, const string& fileName, FileMetadata& meta,
                        vector<Seeder>& seeders, string& errorMsg) = 0;
};
