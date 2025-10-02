#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <map>
#include <mutex>

using namespace std;

#define PIECE_SIZE (512*1024)

#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <map>
#include <mutex>

using namespace std;

#define PIECE_SIZE (512*1024)

// ==================== TRACKER SIDE ====================

// User structure
struct User {
    string userId;
    string password;
    bool loggedIn;
    string ip;
    int port;
    int socketFd;
};

// Group structure
struct Group {
    string groupId;
    string owner;
    unordered_set<string> members;
    unordered_set<string> pendingRequests;
};

// Seeder information
struct SeederInfo {
    string filepath;                 // local path at seeder's machine
    unordered_set<int> pieces;       // pieces this seeder has
};

// File metadata structure
struct FileMeta {
    string filename;
    long long filesize;
    string fileSha1;
    vector<string> pieceSha1;
    int pieceCount;
    unordered_map<string, SeederInfo> seeders;  // key = userId
};

// Global metadata (tracker)
extern unordered_map<string, User> users;
extern unordered_map<string, Group> groups;
extern unordered_map<string, unordered_map<string, FileMeta>> groupFiles;
extern unordered_map<int, string> activeSessions;
extern mutex sessionMutex;
extern mutex metadataMutex;

// Tracker synchronization
extern vector<pair<string, int>> trackersInfo;
extern int myTrackerNo;
extern string myIP;
extern int myPort;
extern int sockfd;
extern int syncSockfd;
extern thread_local bool g_is_sync;

// Command handling
string handleCommand(const string& cmdLine, const string& clientIP, int clientPort, int client_sock);
string handle_upload_file(const string& cmdLine);
string handle_download_file(const string &cmdLine);
string handle_get_file_meta(const string &cmdLine);


void handleDownloadCommand(int clientSock, const string &cmd);
