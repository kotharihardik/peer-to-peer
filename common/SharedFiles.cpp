// SharedFiles.cpp - thread-safe map of files this peer shares
#include "SharedFiles.h"

#include <vector>

using namespace std;

// A file name never contains '/' (it is the last part of a path), so the text
// after the LAST '/' is always the file name -> "scope/file" is a unique key
string SharedFiles::makeKey(const string& scope, const string& fileName) {
    return scope + "/" + fileName;
}

void SharedFiles::add(const string& scope, const string& fileName, const string& path) {
    lock_guard<mutex> lock(mtx);
    files[makeKey(scope, fileName)] = path;
}

void SharedFiles::remove(const string& scope, const string& fileName) {
    lock_guard<mutex> lock(mtx);
    files.erase(makeKey(scope, fileName));
}

bool SharedFiles::find(const string& scope, const string& fileName, string& path) {
    lock_guard<mutex> lock(mtx);
    string key = makeKey(scope, fileName);
    if (!files.count(key)) return false;
    path = files[key];
    return true;
}

// Split each stored key back into (scope, fileName) at the LAST '/'
void SharedFiles::listAll(vector<pair<string, string>>& out) {
    lock_guard<mutex> lock(mtx);
    for (auto& f : files) {
        size_t slash = f.first.find_last_of('/');
        if (slash == string::npos) continue;
        out.push_back(make_pair(f.first.substr(0, slash), f.first.substr(slash + 1)));
    }
}
