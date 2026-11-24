#pragma once
// =====================================================================
// UploadManager - start / stop sharing a file
// =====================================================================
// "Upload" does NOT send the file anywhere. It only:
//   1. hashes the file (every piece hash + the whole-file hash)
//   2. publishes that METADATA to the index (tracker or DHT)
//   3. adds the file to SharedFiles, so PeerServer may serve its pieces
// The data itself moves later, directly to whoever downloads it.

#include "FileMetadata.h"
#include "MetadataIndex.h"
#include "SharedFiles.h"

#include <string>

using namespace std;

class UploadManager {
public:
    UploadManager(MetadataIndex& index, SharedFiles& shared);

    // Hash the file on disk, then share it
    string upload(const string& scope, const string& path);

    // Share when the hashes are already known (used right after a download,
    // so the file is not hashed a second time)
    string share(const string& scope, const FileMetadata& meta, const string& path);

    string stopShare(const string& scope, const string& fileName);

private:
    MetadataIndex& index;
    SharedFiles& shared;
};
