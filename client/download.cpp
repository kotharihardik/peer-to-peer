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


// Global download tracking
map<string, DownloadInfo> activeDownloads;
mutex downloadsMutex;

// Generate unique key for download
string getDownloadKey(const string& groupId, const string& filename) {
    return groupId + "::" + filename;
}

// Show all downloads (both active and completed)
string showDownloads() {
    lock_guard<mutex> lock(downloadsMutex);
    
    if (activeDownloads.empty()) {
        return "No downloads\n";
    }
    
    ostringstream oss;
    
    for (const auto& pair : activeDownloads) {
        const DownloadInfo& dl = pair.second;
        
        // Format: [Status] [groupId] filename
        if (dl.status == COMPLETED) {
            oss << "[C] [" << dl.groupId << "] " << dl.filename << "\n";
        } else if (dl.status == DOWNLOADING) {
            oss << "[D] [" << dl.groupId << "] " << dl.filename << "\n";
        }
        // Skip FAILED status in output (or you can show it as [F])
    }
    
    return oss.str();
}

extern string calculateSHA1(const unsigned char* data, size_t len);

// Rarest-first piece selection
int selectRarestPiece(const vector<bool>& downloaded, const vector<Seeder>& seeders, int totalPieces) {
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
    
    return rarestPiece;
}

// Download single piece from a peer
bool downloadPieceFromPeer(const string& ip, int port, const string& filepath, 
                           int pieceIndex, vector<unsigned char>& pieceData, 
                           const string& expectedHash, long long expectedSize,
                           const string& downloadKey) {
    
    // Check if stop requested
    {
        lock_guard<mutex> lock(downloadsMutex);
        if (activeDownloads.count(downloadKey) && activeDownloads[downloadKey].stopRequested) {
            cout << "[STOPPED] Download cancelled during piece " << pieceIndex << endl;
            return false;
        }
    }
    
    cout << "[DOWNLOAD] Attempting piece " << pieceIndex << " from " << ip << ":" << port << endl;
    
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
            cerr << "[ERROR] Connection lost at " << totalReceived << "/" << pieceSize << " bytes" << endl;
            close(sock);
            return false;
        }
        totalReceived += n;
    }
    
    close(sock);
    
    // Verify piece hash
    string computedHash = calculateSHA1(pieceData.data(), pieceData.size());
    
    if (computedHash != expectedHash) {
        cerr << "[ERROR] Hash mismatch for piece " << pieceIndex << endl;
        return false;
    }
    
    cout << "[SUCCESS] Piece " << pieceIndex << " downloaded and verified (" << pieceSize << " bytes)" << endl;
    
    return true;
}

// Fixed metadata receiver - reads until "END\n"
string recvMetadataUntilEnd(int sockfd) {
    string response;
    char buffer[8192];
    
    while (true) {
        ssize_t n = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) break;
        
        buffer[n] = '\0';
        response.append(buffer, n);
        
        // Check if we received END marker
        if (response.find("END\n") != string::npos) {
            break;
        }
    }
    
    return response;
}

// Main download function with stop support
void downloadFile(string trackerIP, int trackerPort, string groupId, 
                 string filename, string destPath) {
    
    string downloadKey = getDownloadKey(groupId, filename);
    
    cout << "\n==========================================\n";
    cout << "        STARTING DOWNLOAD\n";
    cout << "==========================================\n";
    cout << "[INFO] File: " << filename << "\n";
    cout << "[INFO] Group: " << groupId << "\n";
    cout << "[INFO] Destination: " << destPath << "\n";
    cout << "==========================================\n\n";
    
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
    
    // Receive metadata until END marker
    string response = recvMetadataUntilEnd(sock);
    close(sock);
    
    if (response.empty()) {
        cerr << "[ERROR] No response from tracker" << endl;
        return;
    }
    
    cout << "[TRACKER] Received metadata" << endl;
    
    // Parse metadata
    stringstream ss(response);
    string line;
    
    // First line: OK filesize pieceCount fileSha1
    if (!getline(ss, line)) {
        cerr << "[ERROR] Invalid metadata format" << endl;
        return;
    }
    
    stringstream firstLine(line);
    string okMarker;
    long long filesize;
    int pieceCount;
    string fileSha1;
    firstLine >> okMarker >> filesize >> pieceCount >> fileSha1;
    
    if (okMarker != "OK") {
        cerr << "[ERROR] " << response << endl;
        return;
    }
    
    // Store piece count for tracking
    {
        lock_guard<mutex> lock(downloadsMutex);
        if (activeDownloads.count(downloadKey)) {
            activeDownloads[downloadKey].totalPieces = pieceCount;
        }
    }
    
    // Read piece hashes
    vector<string> pieceHashes;
    for (int i = 0; i < pieceCount; i++) {
        if (!getline(ss, line)) {
            cerr << "[ERROR] Missing piece hash " << i << endl;
            return;
        }
        pieceHashes.push_back(line);
    }
    
    // Read seeders
    // Read seeders
    vector<Seeder> seeders;
    while (getline(ss, line)) {
        if (line.empty() || line == "END") break;
        
        // Parse format: userId ip port filepath pieces
        // But tracker sends: ip:port:filepath:pieces

        cout << line << endl;
        
        // Find colons
        size_t c1 = line.find(':');
        size_t c2 = line.find(':', c1 + 1);
        size_t c3 = line.find(':', c2 + 1);
        
        if (c1 == string::npos || c2 == string::npos || c3 == string::npos) {
            cerr << "[WARN] Malformed seeder line: " << line << endl;
            continue;
        }
        
        Seeder s;
        s.ip = line.substr(0, c1);
        s.port = stoi(line.substr(c1 + 1, c2 - c1 - 1));
        s.filepath = line.substr(c2 + 1, c3 - c2 - 1);  // ← EXTRACT FILEPATH
        
        // Parse pieces (after 3rd colon)
        string piecesStr = line.substr(c3 + 1);
        stringstream ps(piecesStr);
        string p;
        while (getline(ps, p, ',')) {
            if (!p.empty() && p != " ") {
                s.pieces.insert(stoi(p));
            }
        }
        
        seeders.push_back(s);
        cout << "[SEEDER] " << s.ip << ":" << s.port << endl;
        cout << "         Filepath: " << s.filepath << endl;
        cout << "         Pieces: " << s.pieces.size() << endl;
    }
        if (seeders.empty()) {
        cerr << "[ERROR] No seeders available" << endl;
        return;
    }
    
    // Create temp directory for pieces
    string tempDir = "/tmp/dl_" + filename + "_" + to_string(time(nullptr));
    system(("mkdir -p " + tempDir).c_str());
    
    // Track downloaded pieces
    vector<bool> downloaded(pieceCount, false);
    int completedPieces = 0;
    
    cout << "\n==========================================\n";
    cout << "        DOWNLOADING PIECES\n";
    cout << "==========================================\n";
    
    // Download pieces using rarest-first algorithm
    while (completedPieces < pieceCount) {
        // Check if stop requested
        {
            lock_guard<mutex> lock(downloadsMutex);
            if (activeDownloads.count(downloadKey) && activeDownloads[downloadKey].stopRequested) {
                cout << "\n[STOPPED] Download cancelled by user\n";
                system(("rm -rf " + tempDir).c_str());
                return;
            }
        }
        
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
        
        // Try downloading from available seeders
        bool success = false;
        vector<unsigned char> pieceData;
        
        for (const auto& seeder : seeders) {
            if (!seeder.pieces.count(pieceIndex)) continue;
            
            if (downloadPieceFromPeer(seeder.ip, seeder.port, seeder.filepath, 
                                     pieceIndex, pieceData, pieceHashes[pieceIndex], 
                                     expectedSize, downloadKey)) {
                success = true;
                break;
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
            
            // Update tracking
            {
                lock_guard<mutex> lock(downloadsMutex);
                if (activeDownloads.count(downloadKey)) {
                    activeDownloads[downloadKey].completedPieces = completedPieces;
                }
            }
            
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
    cout << "\n==========================================\n";
    cout << "        ASSEMBLING FILE\n";
    cout << "==========================================\n";
    
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
    
    string computedSha = calculateSHA1(fileData.data(), fileData.size());
    
    if (computedSha == fileSha1) {
        cout << "\n==========================================\n";
        cout << "[C] [" << groupId << "] " << filename << endl;
        cout << "==========================================\n";
        cout << "[SUCCESS] File saved to: " << finalPath << endl;
        cout << "[SUCCESS] Size: " << fileData.size() << " bytes" << endl;
        cout << "==========================================\n\n";
    } else {
        cerr << "\n[ERROR] Final file hash mismatch!" << endl;
        cerr << "[ERROR] File may be corrupted" << endl;
    }
    
    // Cleanup temp directory
    cout << "[CLEANUP] Removing temporary files..." << endl;
    system(("rm -rf " + tempDir).c_str());
}