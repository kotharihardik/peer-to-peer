#include "header.h"
#include <fstream>
#include <sstream>
#include <openssl/sha.h>
#include <iomanip>
#include <sys/stat.h>
#include <iostream>
#include <sys/socket.h>

using namespace std;

string calculateSHA1(const unsigned char* data, size_t len) {
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(data, len, hash);
    
    ostringstream oss;
    for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
        oss << hex << setw(2) << setfill('0') << (int)hash[i];
    }
    return oss.str();
}

long long getFileSize(const string& filepath) {
    struct stat st;
    if (stat(filepath.c_str(), &st) == 0) {
        return st.st_size;
    }
    return -1;
}

// CRITICAL: Send ALL data, not just one send() call
bool sendAll(int sockfd, const string& data) {
    size_t totalSent = 0;
    size_t remaining = data.size();
    
    while (totalSent < data.size()) {
        ssize_t sent = send(sockfd, data.c_str() + totalSent, remaining, 0);
        if (sent <= 0) {
            cerr << "[ERROR] send() failed after " << totalSent << " bytes" << endl;
            return false;
        }
        totalSent += sent;
        remaining -= sent;
        
        if (totalSent % 4096 == 0 || totalSent == data.size()) {
            cout << "[UPLOAD] Sent " << totalSent << "/" << data.size() 
                 << " bytes (" << (totalSent * 100 / data.size()) << "%)" << endl;
        }
    }
    
    return true;
}

string send_upload_to_tracker(int sockfd, const string& userId, const string& groupId, 
                               const string& filepath, const string& clientIp, int clientPort) {
    
    ifstream file(filepath, ios::binary);
    if (!file) {
        return "ERROR: File not found\n";
    }
    
    long long filesize = getFileSize(filepath);
    if (filesize < 0) {
        return "ERROR: Cannot get file size\n";
    }
    
    int pieceCount = (filesize + PIECE_SIZE - 1) / PIECE_SIZE;
    
    cout << "[UPLOAD] Processing file: " << filepath << endl;
    cout << "[UPLOAD] Size: " << filesize << " bytes (" << pieceCount << " pieces)" << endl;
    
    // Calculate hashes
    vector<string> pieceSha1;
    vector<unsigned char> fullFileData;
    fullFileData.reserve(filesize);
    
    for (int i = 0; i < pieceCount; i++) {
        size_t pieceSize = PIECE_SIZE;
        if (i == pieceCount - 1) {
            pieceSize = filesize - ((long long)i * PIECE_SIZE);
        }
        
        vector<unsigned char> pieceData(pieceSize);
        file.read((char*)pieceData.data(), pieceSize);
        
        if (file.gcount() != pieceSize) {
            return "ERROR: Failed to read piece " + to_string(i) + "\n";
        }
        
        string pieceSha = calculateSHA1(pieceData.data(), pieceSize);
        pieceSha1.push_back(pieceSha);
        
        fullFileData.insert(fullFileData.end(), pieceData.begin(), pieceData.end());
        
        if ((i + 1) % 100 == 0 || i == pieceCount - 1) {
            cout << "[UPLOAD] Hashed " << (i + 1) << "/" << pieceCount << " pieces" << endl;
        }
    }
    
    file.close();
    
    string fileSha1 = calculateSHA1(fullFileData.data(), fullFileData.size());
    string filename = filepath.substr(filepath.find_last_of("/\\") + 1);
    
    cout << "[UPLOAD] File SHA1: " << fileSha1 << endl;
    
    // Build command
    ostringstream msg;
    msg << "upload_file " << userId << " " << groupId << " " << filename << " "
        << filesize << " " << pieceCount << " " << fileSha1 << " " << filepath;
    
    for (const auto& hash : pieceSha1) {
        msg << " " << hash;
    }
    msg << "\n";
    
    string request = msg.str();
    
    cout << "[UPLOAD] Command size: " << request.size() << " bytes" << endl;
    cout << "[UPLOAD] Sending to tracker..." << endl;
    
    // CRITICAL: Use sendAll to ensure complete transmission
    if (!sendAll(sockfd, request)) {
        return "ERROR: Failed to send complete upload command\n";
    }
    
    cout << "[UPLOAD] Waiting for tracker response..." << endl;
    
    // Receive response
    char buffer[4096];
    ssize_t n = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) {
        return "ERROR: No response from tracker\n";
    }
    
    buffer[n] = '\0';
    return string(buffer);
}