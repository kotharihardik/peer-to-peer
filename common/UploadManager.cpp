// UploadManager.cpp - publish metadata, then serve the file to other peers
#include "UploadManager.h"

#include <climits>
#include <cstdlib>

using namespace std;

UploadManager::UploadManager(MetadataIndex& i, SharedFiles& s) : index(i), shared(s) {
}

string UploadManager::upload(const string& scope, const string& path) {
    // realpath() turns "files/a.txt" into "/home/me/files/a.txt", so serving
    // keeps working even if the process later reads the path differently
    char fullPath[PATH_MAX];
    if (realpath(path.c_str(), fullPath) == NULL) return "ERROR: File not found";

    FileMetadata meta;
    string errorMsg;
    if (!meta.loadFromFile(fullPath, errorMsg)) return "ERROR: " + errorMsg;
    return share(scope, meta, fullPath);
}

string UploadManager::share(const string& scope, const FileMetadata& meta, const string& path) {
    bool ok = false;
    string reply = index.publish(scope, meta, ok);
    // Only start serving the file if the index accepted it
    if (ok) shared.add(scope, meta.name, path);
    return reply;
}

string UploadManager::stopShare(const string& scope, const string& fileName) {
    bool ok = false;
    string reply = index.unpublish(scope, fileName, ok);
    if (ok) shared.remove(scope, fileName);
    return reply;
}
