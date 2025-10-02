#include "header.h"
#include <sstream>
#include <iostream>
#include <vector>

using namespace std;

string handle_upload_file(const string& cmdLine) {
    lock_guard<mutex> lock(metadataMutex);
    
    stringstream ss(cmdLine);
    string cmd, userId, groupId, filename, filepath;
    long long filesize;
    int pieceCount;
    string fileSha1;
    
    ss >> cmd >> userId >> groupId >> filename >> filesize >> pieceCount >> fileSha1 >> filepath;
    
    cout << "[TRACKER] Upload request:" << endl;
    cout << "  User: " << userId << endl;
    cout << "  Group: " << groupId << endl;
    cout << "  File: " << filename << endl;
    cout << "  Size: " << filesize << " bytes" << endl;
    cout << "  Pieces: " << pieceCount << endl;
    cout << "  Command length: " << cmdLine.size() << " bytes" << endl;
    
    // Verify user and group
    if (!users.count(userId) || !users[userId].loggedIn) {
        return "ERROR: User not logged in\n";
    }
    
    if (!groups.count(groupId)) {
        return "ERROR: Group does not exist\n";
    }
    
    if (!groups[groupId].members.count(userId)) {
        return "ERROR: User not a member of group\n";
    }
    
    // Read ALL piece hashes
    vector<string> pieceSha1;
    pieceSha1.reserve(pieceCount);
    
    string hash;
    int hashesRead = 0;
    
    while (ss >> hash && hashesRead < pieceCount) {
        // Verify hash format
        if (hash.size() != 40) {
            cerr << "[ERROR] Invalid hash #" << hashesRead 
                 << " (length " << hash.size() << "): " << hash << endl;
            return "ERROR: Invalid hash format\n";
        }
        
        pieceSha1.push_back(hash);
        hashesRead++;
    }
    
    cout << "[TRACKER] Read " << hashesRead << "/" << pieceCount << " piece hashes" << endl;
    
    if (hashesRead != pieceCount) {
        cerr << "[ERROR] Expected " << pieceCount << " hashes, got " << hashesRead << endl;
        cerr << "[ERROR] This means the upload command was truncated!" << endl;
        return "ERROR: Incomplete hash data - upload command truncated\n";
    }
    
    // Verify all hashes are valid
    for (int i = 0; i < pieceCount; i++) {
        if (pieceSha1[i].empty() || pieceSha1[i].size() != 40) {
            cerr << "[ERROR] Hash #" << i << " is invalid" << endl;
            return "ERROR: Invalid piece hash\n";
        }
    }
    
    cout << "[TRACKER] All " << pieceCount << " hashes validated ✓" << endl;
    
    // Store metadata
    FileMeta meta;
    meta.filename = filename;
    meta.filesize = filesize;
    meta.fileSha1 = fileSha1;
    meta.pieceCount = pieceCount;
    meta.pieceSha1 = pieceSha1;
    
    SeederInfo seeder;
    seeder.filepath = filepath;
    for (int i = 0; i < pieceCount; i++) {
        seeder.pieces.insert(i);
    }
    
    if (groupFiles[groupId].count(filename)) {
        groupFiles[groupId][filename].seeders[userId] = seeder;
        cout << "[TRACKER] Added " << userId << " as additional seeder" << endl;
    } else {
        meta.seeders[userId] = seeder;
        groupFiles[groupId][filename] = meta;
        cout << "[TRACKER] Created new file entry" << endl;
    }
    
    return "SUCCESS: File shared with group\n";
}