// TrackerIndex.cpp - talks the tracker's text protocol
#include "TrackerIndex.h"
#include "Utils.h"

#include <sstream>

using namespace std;

TrackerIndex::TrackerIndex(TrackerClient& t) : tracker(t) {
}

// Sends: upload_file <gid> <file> <size> <sha1> <pieceCount> <hash1> ... <hashN>
// (a 1 GB file has 2048 pieces -> about 84 KB of hashes, fine in one message)
string TrackerIndex::publish(const string& scope, const FileMetadata& meta, bool& ok) {
    stringstream cmd;
    cmd << "upload_file " << scope << " " << meta.name << " " << meta.size << " " << meta.sha1 << " "
        << meta.pieceHashes.size();
    for (auto& h : meta.pieceHashes) cmd << " " << h;

    string reply = tracker.request(cmd.str());
    ok = reply.substr(0, 7) == "SUCCESS";
    return reply;
}

string TrackerIndex::unpublish(const string& scope, const string& fileName, bool& ok) {
    string reply = tracker.request("stop_share " + scope + " " + fileName);
    ok = reply.substr(0, 7) == "SUCCESS";
    return reply;
}

// Sends:    download_file <gid> <file>
// Receives: OK <size> <pieceCount> <sha1>
//           <piece hash>        (one line per piece)
//           <uid> <ip> <port>   (one line per ONLINE seeder)
bool TrackerIndex::lookup(const string& scope, const string& fileName, FileMetadata& meta,
                          vector<Seeder>& seeders, string& errorMsg) {
    string reply = tracker.request("download_file " + scope + " " + fileName);

    vector<string> lines = splitLines(reply);
    vector<string> first;
    if (!lines.empty()) first = splitWords(lines[0]);

    long long count = 0;
    bool valid = first.size() == 4 && first[0] == "OK" && parseInt(first[1], meta.size) &&
                 parseInt(first[2], count) && count >= 0 && (long long)lines.size() >= 1 + count;
    if (!valid) {
        if (reply.substr(0, 5) == "ERROR") errorMsg = reply;   // e.g. "File not found in group"
        else errorMsg = "ERROR: Malformed reply from tracker";
        return false;
    }

    meta.name = fileName;
    meta.sha1 = first[3];
    for (long long i = 1; i <= count; i++) meta.pieceHashes.push_back(lines[i]);

    // Every line after the hashes describes one online seeder
    for (int i = 1 + count; i < (int)lines.size(); i++) {
        vector<string> w = splitWords(lines[i]);
        long long port = 0;
        if (w.size() != 3 || !parseInt(w[2], port)) continue;

        Seeder s;
        s.peerId = w[0];
        s.address = Address(w[1], (int)port);
        seeders.push_back(s);
    }
    if (seeders.empty()) {
        errorMsg = "ERROR: No other seeder is online for this file";
        return false;
    }
    return true;
}
