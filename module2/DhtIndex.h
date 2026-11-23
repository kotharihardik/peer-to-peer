#pragma once
// =====================================================================
// DhtIndex - module 2's answer to "who has this file?"
// =====================================================================
// This class implements the SAME MetadataIndex interface that module 1's
// TrackerIndex implements. That is the whole point of the second generation:
// the upload and download machinery in common/ is untouched, and only the way
// metadata is found changes.
//
//   TrackerIndex : one TCP request to the one tracker that knows everything
//   DhtIndex     : hash the file name to a key, route through the overlay to
//                  the node responsible for that key, ask that node
//
// It also keeps a copy of the metadata for the files THIS peer shares, so it
// can re-announce them every ANNOUNCE_SEC seconds without hashing the files
// again. Re-announcing is what makes the index self-healing: after any node
// joins, leaves or crashes, the records reappear on whichever node is
// responsible now.

#include "DhtNode.h"
#include "MetadataIndex.h"

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace std;

// How often this peer re-publishes its files (must be well below ENTRY_TTL_SEC)
const int ANNOUNCE_SEC = 20;

class DhtIndex : public MetadataIndex {
public:
    DhtIndex(DhtNode& node);
    ~DhtIndex();

    void start();   // begins re-announcing in the background
    void stop();

    string publish(const string& scope, const FileMetadata& meta, bool& ok) override;
    string unpublish(const string& scope, const string& fileName, bool& ok) override;
    bool lookup(const string& scope, const string& fileName, FileMetadata& meta, vector<Seeder>& seeders,
                string& errorMsg) override;

    // The "search" command: a lookup whose result is printed for the user
    string search(const string& scope, const string& fileName);

private:
    // One file this peer shares, kept so it can be announced again
    struct MyFile {
        string scope;
        FileMetadata meta;
    };

    void announceLoop();
    // Route to the responsible node and print the hop trace when verbose
    NodeInfo findOwner(unsigned int key, const string& what, int& hops, bool verbose);
    // Send the metadata to whichever node owns the key
    bool storeOne(const string& scope, const FileMetadata& meta, bool verbose, string& message);

    DhtNode& node;

    mutex mtx;
    map<string, MyFile> myFiles;   // key = scope + "/" + file name

    thread announcer;
    atomic<bool> running;
};
