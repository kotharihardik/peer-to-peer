// DownloadManager.cpp - look up seeders, then run the download in the background
#include "DownloadManager.h"

#include <sys/stat.h>

using namespace std;

DownloadManager::DownloadManager(MetadataIndex& i, UploadManager& u, Address me) : index(i), uploads(u) {
    self = me;
}

// Shutdown: first tell ALL downloads to stop (so they stop in parallel),
// then clear the list -> each ~Download() waits for its threads.
DownloadManager::~DownloadManager() {
    lock_guard<mutex> lock(mtx);
    for (auto& d : downloads) d->cancel();
    downloads.clear();
}

string DownloadManager::startDownload(const string& scope, const string& fileName, const string& dest) {
    // 1. Reject a duplicate while the same file is still downloading
    {
        lock_guard<mutex> lock(mtx);
        for (auto& d : downloads) {
            if (d->isActive() && d->getScope() == scope && d->getFileName() == fileName) {
                return "ERROR: This file is already being downloaded";
            }
        }
    }

    // 2. Ask the index (tracker or DHT) for the hashes and the seeder list
    FileMetadata meta;
    vector<Seeder> found;
    string errorMsg;
    if (!index.lookup(scope, fileName, meta, found, errorMsg)) return errorMsg;

    // 3. Never download from myself (I may already be a seeder of this file)
    vector<Seeder> seeders;
    for (auto& s : found) {
        if (s.address.ip == self.ip && s.address.port == self.port) continue;
        seeders.push_back(s);
    }
    if (seeders.empty()) return "ERROR: No other seeder is online for this file";

    // 4. Destination is an existing folder -> put the file inside it
    string path = dest;
    struct stat st;
    if (stat(dest.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        if (path.back() != '/') path += "/";
        path += fileName;
    }

    // 5. Create the download and start it in the background
    unique_ptr<Download> download(new Download(scope, meta, seeders, path, uploads));
    if (!download->createOutputFile(errorMsg)) return "ERROR: " + errorMsg;
    download->start();

    // 6. Remember it (replacing an old finished entry for the same file)
    lock_guard<mutex> lock(mtx);
    for (int i = 0; i < (int)downloads.size(); i++) {
        if (!downloads[i]->isActive() && downloads[i]->getScope() == scope &&
            downloads[i]->getFileName() == fileName) {
            downloads.erase(downloads.begin() + i);
            break;
        }
    }
    // release() = the unique_ptr gives up ownership, the list becomes the owner
    downloads.push_back(unique_ptr<Download>(download.release()));
    return "Download started: " + fileName + " from " + to_string(seeders.size()) + " seeder(s) -> " + path;
}

string DownloadManager::showDownloads() {
    lock_guard<mutex> lock(mtx);
    if (downloads.empty()) return "No downloads";
    string result;
    for (auto& d : downloads) result += d->getStatusLine() + "\n";
    result.pop_back();
    return result;
}
