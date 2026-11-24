#pragma once
// =====================================================================
// SharedFiles - list of files this peer is sharing (thread-safe)
// =====================================================================
//   (scope, file name)  ->  path on my disk
//
// The PeerServer only serves files in this list. Other peers request by
// (scope, file name); the local path is looked up here and never travels over
// the network, so a remote peer cannot ask for an arbitrary file on my disk.

#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

using namespace std;

class SharedFiles {
public:
    void add(const string& scope, const string& fileName, const string& path);
    void remove(const string& scope, const string& fileName);
    bool find(const string& scope, const string& fileName, string& path);

    // Every (scope, fileName) I currently share - used by module 2 to
    // re-announce its files into the DHT periodically.
    void listAll(vector<pair<string, string>>& out);

private:
    // The command thread adds/removes while peer-server threads read -> mutex
    mutex mtx;
    map<string, string> files;   // key = scope + "/" + fileName

    string makeKey(const string& scope, const string& fileName);
};
