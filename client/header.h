#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <map>
#include <atomic>

using namespace std;

#define PIECE_SIZE (512*1024)

// -------------------- Download status tracking --------------------
enum DownloadStatus {
    DOWNLOADING,
    COMPLETED,
    FAILED
};

// -------------------- DownloadInfo --------------------
struct DownloadInfo {
    string groupId;
    string filename;
    string destPath;
    DownloadStatus status;
    int totalPieces = 0;
    atomic<int> completedPieces;
    atomic<bool> stopRequested;

    DownloadInfo() : status(DOWNLOADING), totalPieces(0),
                     completedPieces(0), stopRequested(false) {}

    // Copy constructor
    DownloadInfo(const DownloadInfo& other) {
        groupId = other.groupId;
        filename = other.filename;
        destPath = other.destPath;
        status = other.status;
        totalPieces = other.totalPieces;
        completedPieces.store(other.completedPieces.load());
        stopRequested.store(other.stopRequested.load());
    }

    // Assignment operator
    DownloadInfo& operator=(const DownloadInfo& other) {
        if (this != &other) {
            groupId = other.groupId;
            filename = other.filename;
            destPath = other.destPath;
            status = other.status;
            totalPieces = other.totalPieces;
            completedPieces.store(other.completedPieces.load());
            stopRequested.store(other.stopRequested.load());
        }
        return *this;
    }
};

// -------------------- Seeder info --------------------
struct Seeder {
    string userId;
    string ip;
    int port;
    string filepath;
    unordered_set<int> pieces;
};

// -------------------- Global state --------------------
extern string g_trackerIP;
extern int g_trackerPort;

extern map<string, DownloadInfo> activeDownloads; // key = groupId+filename
extern mutex downloadsMutex;

// -------------------- Function declarations --------------------
string send_upload_to_tracker(int sockfd, const string& userId, const string& groupId,
                              const string& filepath, const string& clientIp, int clientPort);

void downloadFile(string trackerIP, int trackerPort, string groupId,
                  string filename, string destPath);

void startPeerServer(int port);

string calculateSHA1(const unsigned char* data, size_t len);
long long getFileSize(const string& filepath);

// New functions for download management
string showDownloads();
string stopDownload(const string& groupId, const string& filename);
string getDownloadKey(const string& groupId, const string& filename);
