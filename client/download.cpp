#include "header.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <unordered_set>
#include <sstream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>
#include <openssl/sha.h>
#include <iomanip>
#include <map>
#include <algorithm>

using namespace std;

// External SHA1 function
extern string calculateSHA1(const unsigned char* data, size_t len);

// Rarest-first piece selection
int selectRarestPiece(const vector<bool>& downloaded, const vector<Seeder>& seeders, int totalPieces) {
    // Count how many seeders have each piece
    map<int, int> pieceAvailability;
    
    for (int i = 0; i < totalPieces; i++) {
        if (!downloaded[i]) {
            pieceAvailability[i] = 0;
            for (const auto& seeder : seeders) {
                if (seeder.pieces.count(i)) {
                    pieceAvailability[i]++;
                }
            }
        }
    }
    
    if (pieceAvailability.empty()) {
        return -1; // All pieces downloaded
    }
    
    // Find rarest piece (lowest availability)
    int rarestPiece = -1;
    int minCount = INT_MAX;
    
    for (const auto& pa : pieceAvailability) {
        if (pa.second > 0 && pa.second < minCount) {
            minCount = pa.second;
            rarestPiece = pa.first;
        }
    }
    
    cout << "[PIECE_SELECT] Selected piece " << rarestPiece 
         << " (rarity: " << minCount << " seeders)" << endl;
    
    return rarestPiece;
}

// Download single piece from a peer
bool downloadPieceFromPeer(const string& ip, int port, const string& filepath, 
                           int pieceIndex, vector<unsigned char>& pieceData, 
                           const string& expectedHash, long long expectedSize) {
    
    cout << "[DOWNLOAD] Attempting piece " << pieceIndex << " from " 
         << ip << ":" << port << endl;
    cout << "[DOWNLOAD]   Filepath: " << filepath << endl;
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        cerr << "[ERROR] Socket creation failed" << endl;
        return false;
    }
    
    // Set timeout
    struct timeval tv;
    tv.tv_sec = 30;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        cerr << "[ERROR] Connection failed to " << ip << ":" << port << endl;
        close(sock);
        return false;
    }
    
    // Send request: GET_PIECE filepath pieceIndex
    ostringstream req;
    req << "GET_PIECE " << filepath << " " << pieceIndex << "\n";
    string request = req.str();
    
    cout << "[SEND] " << request;
    
    if (send(sock, request.c_str(), request.size(), 0) < 0) {
        cerr << "[ERROR] Send failed" << endl;
        close(sock);
        return false;
    }
    
    // Receive piece size first (8 bytes)
    long long pieceSize = 0;
    ssize_t n = recv(sock, &pieceSize, sizeof(pieceSize), MSG_WAITALL);
    if (n != sizeof(pieceSize)) {
        cerr << "[ERROR] Failed to receive piece size" << endl;
        close(sock);
        return false;
    }
    
    cout << "[RECV] Piece size: " << pieceSize << " bytes" << endl;
    
    if (pieceSize <= 0 || pieceSize > PIECE_SIZE) {
        cerr << "[ERROR] Invalid piece size: " << pieceSize << endl;
        close(sock);
        return false;
    }
    
    // Receive piece data
    pieceData.resize(pieceSize);
    size_t totalReceived = 0;
    
    while (totalReceived < pieceSize) {
        n = recv(sock, pieceData.data() + totalReceived, pieceSize - totalReceived, 0);
        if (n <= 0) {
            cerr << "[ERROR] Connection lost at " << totalReceived 
                 << "/" << pieceSize << " bytes" << endl;
            close(sock);
            return false;
        }
        totalReceived += n;
        
        if (totalReceived % (100*1024) == 0 || totalReceived == pieceSize) {
            cout << "[PROGRESS] Received " << totalReceived << "/" << pieceSize 
                 << " bytes (" << (totalReceived * 100 / pieceSize) << "%)" << endl;
        }
    }
    
    close(sock);
    
    // Verify piece hash
    string computedHash = calculateSHA1(pieceData.data(), pieceData.size());
    
    cout << "[VERIFY] Expected: " << expectedHash << endl;
    cout << "[VERIFY] Computed: " << computedHash << endl;
    
    if (computedHash != expectedHash) {
        cerr << "[ERROR] Hash mismatch for piece " << pieceIndex << endl;
        return false;
    }
    
    cout << "[SUCCESS] Piece " << pieceIndex << " downloaded and verified (" 
         << pieceSize << " bytes)" << endl;
    cout << "-------------------------------------------" << endl;
    
    return true;
}

string recvAllMetadata(int sockfd, int num_chunks) {
    string response;
    char buf[32768];
    int n;

    // keep reading until we collect enough tokens
    while (true) {
        n = recv(sockfd, buf, sizeof(buf), 0);
        if (n <= 0) break; // connection closed
        response.append(buf, n);

        // quick check: count how many tokens we got
        int tokenCount = 0;
        for (char c : response) {
            if (isspace(c)) tokenCount++;
        }

        // (filesize + num_chunks + all hashes)
        // we need at least num_chunks + 2 tokens
        if (tokenCount >= num_chunks + 2) {
            break;
        }
    }

    return response;
}


// Main download function
void downloadFile(string trackerIP, int trackerPort, string groupId, 
                 string filename, string destPath) {
    
    cout << "\n==========================================" << endl;
    cout << "        STARTING DOWNLOAD                  " << endl;
    cout << "==========================================" << endl;
    cout << "[INFO] File: " << filename << endl;
    cout << "[INFO] Group: " << groupId << endl;
    cout << "[INFO] Destination: " << destPath << endl;
    cout << "==========================================\n" << endl;
    
    // Connect to tracker
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        cerr << "[ERROR] Socket creation failed" << endl;
        return;
    }
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(trackerPort);
    addr.sin_addr.s_addr = inet_addr(trackerIP.c_str());
    
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        cerr << "[ERROR] Cannot connect to tracker" << endl;
        close(sock);
        return;
    }
    
    cout << "[TRACKER] Connected to " << trackerIP << ":" << trackerPort << endl;
    
    // Request file metadata
    string cmd = "download_file " + groupId + " " + filename + "\n";
    send(sock, cmd.c_str(), cmd.size(), 0);
    
    // Receive metadata (may be large response)
    string response;
    char buffer[8192];
    while (true) {
        ssize_t n = recv(sock, buffer, sizeof(buffer), 0);

        cout << "inside loop ------" << endl;
        if (n < 0) { cerr << "recv failed\n"; break; }
        if (n == 0) break;  // socket closed
        response.append(buffer, n);
        cout << response << endl;
    }

    
//    string response;
//     char buffer[8192];
//     while (true) {
//         ssize_t n = recv(sock, buffer, sizeof(buffer), 0);
//         if (n <= 0) break;  // stop on error or EOF
//         response.append(buffer, n);
//     }


    cout << "response --" << response;

    close(sock);
    
    if (response.empty()) {
        cerr << "[ERROR] No response from tracker" << endl;
        return;
    }
    
    cout << "[TRACKER] Received metadata (" << response.size() << " bytes)" << endl;

    // cout << "meta data ---" << response << endl;
    
    stringstream ss(response);
    string line;

    // First line: filesize pieceCount fileSha1
    if (!getline(ss, line)) {
        cerr << "[ERROR] Invalid metadata format" << endl;
        return;
    }

    stringstream liness(line);
    long long filesize;
    int pieceCount;
    string fileSha1;
    liness >> filesize >> pieceCount >> fileSha1;

    // Second line: piece hashes
    if (!getline(ss, line)) {
        cerr << "[ERROR] Missing piece hashes" << endl;
        return;
    }

    stringstream phss(line);
    vector<string> pieceHashes;
    string h;
    while (phss >> h) pieceHashes.push_back(h);

    cout << "[METADATA] Loaded " << pieceHashes.size() << " piece hashes" << endl;

    // Remaining lines: seeders
    vector<Seeder> seeders;
    while (getline(ss, line)) {
        if (line.empty()) continue;

        Seeder s;
        size_t c1 = line.find(':');
        size_t c2 = line.find(':', c1 + 1);
        size_t c3 = line.find(':', c2 + 1);
        if (c1 == string::npos || c2 == string::npos || c3 == string::npos) continue;

        s.ip = line.substr(0, c1);
        s.port = stoi(line.substr(c1 + 1, c2 - c1 - 1));
        s.filepath = line.substr(c2 + 1, c3 - c2 - 1);

        string piecesStr = line.substr(c3 + 1);
        stringstream ps(piecesStr);
        string p;
        while (getline(ps, p, ',')) {
            if (!p.empty()) s.pieces.insert(stoi(p));
        }

        seeders.push_back(s);

        cout << "[SEEDER] " << s.ip << ":" << s.port << endl;
        cout << "         Filepath: " << s.filepath << endl;
        cout << "         Pieces: " << s.pieces.size() << " available" << endl;
    }


    if (seeders.empty()) {
        cerr << "[ERROR] No seeders available" << endl;
        return;
    }

    
    cout << "[SEEDERS] Total: " << seeders.size() << " seeder(s) found" << endl;
    
    // Create temp directory for pieces
    string tempDir = "/tmp/dl_" + filename + "_" + to_string(time(nullptr));
    system(("mkdir -p " + tempDir).c_str());
    cout << "\n[TEMP] Created directory: " << tempDir << endl;
    
    // Track downloaded pieces
    vector<bool> downloaded(pieceCount, false);
    int completedPieces = 0;
    
    cout << "\n==========================================" << endl;
    cout << "        DOWNLOADING PIECES                 " << endl;
    cout << "==========================================" << endl;
    
    // Download pieces using rarest-first algorithm
    while (completedPieces < pieceCount) {
        // Select rarest piece
        int pieceIndex = selectRarestPiece(downloaded, seeders, pieceCount);
        
        if (pieceIndex == -1) {
            cerr << "[ERROR] No more pieces available but download incomplete" << endl;
            break;
        }
        
        // Calculate expected piece size
        long long expectedSize = PIECE_SIZE;
        if (pieceIndex == pieceCount - 1) {
            expectedSize = filesize - ((long long)pieceIndex * PIECE_SIZE);
        }
        
        cout << "\n[PIECE " << pieceIndex << "/" << (pieceCount-1) << "]" << endl;
        cout << "[INFO] Expected size: " << expectedSize << " bytes" << endl;
        
        // Try downloading from available seeders
        bool success = false;
        vector<unsigned char> pieceData;
        
        for (const auto& seeder : seeders) {
            if (!seeder.pieces.count(pieceIndex)) {
                continue;
            }
            
            if (downloadPieceFromPeer(seeder.ip, seeder.port, seeder.filepath, 
                                     pieceIndex, pieceData, pieceHashes[pieceIndex], 
                                     expectedSize)) {
                success = true;
                break;
            } else {
                cout << "[RETRY] Trying next seeder..." << endl;
            }
        }
        
        if (success) {
            // Save piece to temp file
            string piecePath = tempDir + "/piece_" + to_string(pieceIndex);
            ofstream pieceFile(piecePath, ios::binary);
            
            if (!pieceFile) {
                cerr << "[ERROR] Cannot write piece file: " << piecePath << endl;
                continue;
            }
            
            pieceFile.write((char*)pieceData.data(), pieceData.size());
            pieceFile.close();
            
            downloaded[pieceIndex] = true;
            completedPieces++;
            
            int progress = (completedPieces * 100) / pieceCount;
            cout << "\n[OVERALL PROGRESS] " << completedPieces << "/" << pieceCount 
                 << " pieces (" << progress << "%)" << endl;
        } else {
            cerr << "[ERROR] Failed to download piece " << pieceIndex 
                 << " from all seeders" << endl;
            break;
        }
    }
    
    if (completedPieces < pieceCount) {
        cerr << "\n[FAILED] Download incomplete: " << completedPieces 
             << "/" << pieceCount << " pieces" << endl;
        system(("rm -rf " + tempDir).c_str());
        return;
    }
    
    // Assemble final file
    cout << "\n==========================================" << endl;
    cout << "        ASSEMBLING FILE                    " << endl;
    cout << "==========================================" << endl;
    
    // Determine final path
    struct stat info;
    string finalPath = destPath;
    if (stat(destPath.c_str(), &info) == 0 && (info.st_mode & S_IFDIR)) {
        if (destPath.back() != '/') finalPath += "/";
        finalPath += filename;
    }
    
    cout << "[ASSEMBLY] Output: " << finalPath << endl;
    
    ofstream outFile(finalPath, ios::binary);
    if (!outFile) {
        cerr << "[ERROR] Cannot create output file: " << finalPath << endl;
        return;
    }
    
    for (int i = 0; i < pieceCount; i++) {
        string piecePath = tempDir + "/piece_" + to_string(i);
        ifstream pieceFile(piecePath, ios::binary);
        
        if (!pieceFile) {
            cerr << "[ERROR] Missing piece " << i << endl;
            outFile.close();
            return;
        }
        
        vector<char> buffer(PIECE_SIZE);
        pieceFile.read(buffer.data(), PIECE_SIZE);
        streamsize bytesRead = pieceFile.gcount();
        outFile.write(buffer.data(), bytesRead);
        pieceFile.close();
        
        cout << "[ASSEMBLY] Merged piece " << i << " (" << bytesRead << " bytes)" << endl;
    }
    
    outFile.close();
    
    // Verify final file
    cout << "\n[VERIFY] Checking final file integrity..." << endl;
    
    ifstream finalFile(finalPath, ios::binary);
    if (!finalFile) {
        cerr << "[ERROR] Cannot open final file for verification" << endl;
        return;
    }
    
    vector<unsigned char> fileData((istreambuf_iterator<char>(finalFile)), 
                                   istreambuf_iterator<char>());
    finalFile.close();
    
    cout << "[VERIFY] Read " << fileData.size() << " bytes from final file" << endl;
    
    string computedSha = calculateSHA1(fileData.data(), fileData.size());
    
    cout << "[VERIFY] Expected SHA1: " << fileSha1 << endl;
    cout << "[VERIFY] Computed SHA1: " << computedSha << endl;
    
    if (computedSha == fileSha1) {
        cout << "\n==========================================" << endl;
        cout << "[C] [" << groupId << "] " << filename << endl;
        cout << "==========================================" << endl;
        cout << "[SUCCESS] File saved to: " << finalPath << endl;
        cout << "[SUCCESS] Size: " << fileData.size() << " bytes" << endl;
        cout << "==========================================\n" << endl;
    } else {
        cerr << "\n[ERROR] Final file hash mismatch!" << endl;
        cerr << "[ERROR] File may be corrupted" << endl;
    }
    
    // Cleanup temp directory
    cout << "[CLEANUP] Removing temporary files..." << endl;
    system(("rm -rf " + tempDir).c_str());
}