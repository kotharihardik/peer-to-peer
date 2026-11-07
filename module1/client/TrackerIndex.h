#pragma once
// =====================================================================
// TrackerIndex - MODULE 1's index: one central tracker knows everything
// =====================================================================
// This is the FIRST-GENERATION answer to "who has this file?":
// ask the central server. All tracker wire format lives in this one class,
// so the shared upload/download code in common/ stays generic.
//
// Module 2 replaces exactly this class with DhtIndex and changes nothing else.

#include "MetadataIndex.h"
#include "TrackerClient.h"

#include <string>
#include <vector>

using namespace std;

class TrackerIndex : public MetadataIndex {
public:
    TrackerIndex(TrackerClient& tracker);

    string publish(const string& scope, const FileMetadata& meta, bool& ok) override;
    string unpublish(const string& scope, const string& fileName, bool& ok) override;
    bool lookup(const string& scope, const string& fileName, FileMetadata& meta,
                vector<Seeder>& seeders, string& errorMsg) override;

private:
    TrackerClient& tracker;
};
