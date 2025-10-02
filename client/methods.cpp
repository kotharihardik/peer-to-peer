
#include <fstream>
#include <sstream>
#include <openssl/sha.h>
#include <iomanip>
#include <sys/stat.h>
#include <iostream>

#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <unordered_set>
#include <string>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sstream>
#include <fstream>
#include <thread>
#include "header.h"







using namespace std;

// Calculate SHA1 hash of data
string calculateSHA1(const unsigned char* data, size_t len) {
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(data, len, hash);
    
    ostringstream oss;
    for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
        oss << hex << setw(2) << setfill('0') << (int)hash[i];
    }
    return oss.str();
}

// Get file size
long long getFileSize(const string& filepath) {
    struct stat st;
    if (stat(filepath.c_str(), &st) == 0) {
        return st.st_size;
    }
    return -1;
}

// Client-side: prepare upload metadata and send to tracker
string send_upload_to_tracker(int sockfd, const string& userId, const string& groupId, 
                               const string& filepath, const string& clientIp, int clientPort) {
    
    // Check if file exists
    ifstream file(filepath, ios::binary);
    if (!file) {
        return "ERROR: File not found\n";
    }
    
    long long filesize = getFileSize(filepath);
    if (filesize < 0) {
        return "ERROR: Cannot get file size\n";
    }
    
    // Calculate piece count
    int pieceCount = (filesize + PIECE_SIZE - 1) / PIECE_SIZE;
    
    // Calculate SHA1 for each piece and entire file
    vector<string> pieceSha1;
    vector<unsigned char> fullFileData;
    
    for (int i = 0; i < pieceCount; i++) {
        size_t pieceSize = PIECE_SIZE;
        if (i == pieceCount - 1) {
            pieceSize = filesize - (i * PIECE_SIZE);
        }
        
        vector<unsigned char> pieceData(pieceSize);
        file.read((char*)pieceData.data(), pieceSize);
        
        string pieceSha = calculateSHA1(pieceData.data(), pieceSize);
        pieceSha1.push_back(pieceSha);
        
        fullFileData.insert(fullFileData.end(), pieceData.begin(), pieceData.end());
    }
    
    file.close();
    
    // Calculate file SHA1
    string fileSha1 = calculateSHA1(fullFileData.data(), fullFileData.size());
    
    // Extract filename
    string filename = filepath.substr(filepath.find_last_of("/\\") + 1);
    
    // Build message to tracker
    ostringstream msg;
    msg << "upload_file " << userId << " " << groupId << " " << filename << " "
        << filesize << " " << pieceCount << " " << fileSha1 << " " << filepath;
    
    // Append piece hashes
    for (const auto& hash : pieceSha1) {
        msg << " " << hash;
    }
    msg << "\n";
    
    string request = msg.str();
    send(sockfd, request.c_str(), request.size(), 0);
    
    // Receive response
    char buffer[4096];
    ssize_t n = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) {
        return "ERROR: Connection lost\n";
    }
    
    buffer[n] = '\0';
    return string(buffer);
}