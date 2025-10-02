#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <map>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <functional>
#include <chrono>


using namespace std;

#define PIECE_SIZE (512*1024)


// ==================== CLIENT SIDE ====================

// Seeder info for download
struct Seeder {
    string userId;
    string ip;
    int port;
    string filepath;
    unordered_set<int> pieces;
};

// Upload/Download functions
string send_upload_to_tracker(int sockfd, const string& userId, const string& groupId, 
                              const string& filepath, const string& clientIp, int clientPort);

void downloadFileFromPeers(const string& filename, const string& destPath,
                          long long filesize, const vector<string>& pieceHashes,
                          const vector<Seeder>& seeders, const string& fileSha1);

string showDownloads();

// Peer server functions
void startPeerServer(int port);
void addSharedFile(const string& filename, const string& filepath);
void removeSharedFile(const string& filename);

// Tracker-side upload handler
string handle_upload_file(const string& cmdLine);

// Utility functions
string calculateSHA1(const unsigned char* data, size_t len);
long long getFileSize(const string& filepath);

// Helper function to receive full message
// inline string recvAll(int sockfd) {
//     string result;
//     char buffer[4096];
    
//     while (true) {
//         ssize_t n = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
//         if (n <= 0) break;
        
//         buffer[n] = '\0';
//         result += buffer;
        
//         // Check if we have complete message (ends with newline or specific marker)
//         if (result.find('\n') != string::npos || result.find("END") != string::npos) {
//             break;
//         }
//     }
    
//     return result;
// }