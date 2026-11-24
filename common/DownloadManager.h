#pragma once
// =====================================================================
// DownloadManager - starts and keeps track of ALL downloads of a peer
// =====================================================================
// Each Download runs on its own threads, so several files can download at
// the same time while the user keeps typing commands.
// A second download of the same file is rejected while the first is running.
//
// It finds seeders through the MetadataIndex, so the same class works with a
// central tracker (module 1) and with the DHT (module 2).

#include "Download.h"
#include "MetadataIndex.h"
#include "UploadManager.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

using namespace std;

class DownloadManager {
public:
    DownloadManager(MetadataIndex& index, UploadManager& uploads, Address self);
    ~DownloadManager();   // cancels running downloads and waits for them

    string startDownload(const string& scope, const string& fileName, const string& dest);
    string showDownloads();

private:
    MetadataIndex& index;
    UploadManager& uploads;
    Address self;   // my own transfer address (never download from myself)

    // unique_ptr: each Download is deleted automatically when removed from the list
    mutex mtx;
    vector<unique_ptr<Download>> downloads;
};
