// #include "header.h"
// #include <unordered_map>
// #include <vector>
// #include <string>
// #include <mutex>

// std::vector<std::pair<std::string,int>> trackersInfo;  // client ports from tracker_info.txt
// int myTrackerNo = -1;
// std::string myIP = "";
// int myPort = 0;

// std::unordered_map<int, std::string> activeSessions;
// std::mutex sessionMutex;

// int sockfd = -1;      // client listener
// int syncSockfd = -1;  // sync listener

// // thread-local marker for commands received from peer
// thread_local bool g_is_sync = false;

// // metadata
// unordered_map<std::string, User> users;
// unordered_map<std::string, Group> groups;
// std::unordered_map<std::string, std::unordered_map<std::string, FileMeta>> groupFiles;
// std::mutex metadataMutex;

// // Mutex for protecting groupFiles
// extern std::mutex groupFilesMutex;

#include "header.h"

// Global metadata storage
unordered_map<string, User> users;
unordered_map<string, Group> groups;
unordered_map<string, unordered_map<string, FileMeta>> groupFiles;

// Session management
unordered_map<int, string> activeSessions;
mutex sessionMutex;
mutex metadataMutex;

// Tracker synchronization
vector<pair<string, int>> trackersInfo;
int myTrackerNo = 0;
string myIP;
int myPort;
int sockfd = -1;
int syncSockfd = -1;
thread_local bool g_is_sync = false;
