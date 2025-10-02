#include "header.h"
#include <iostream>
#include <fstream>
#include <thread>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sstream>

using namespace std;

// NO shared files map needed - tracker stores filepaths!
// Peers receive the filepath directly in GET_PIECE request

// Handle incoming GET_PIECE requests from other peers
void handlePeerRequest(int clientSock) {
    char buffer[4096];
    ssize_t n = recv(clientSock, buffer, sizeof(buffer) - 1, 0);
    
    if (n <= 0) {
        close(clientSock);
        return;
    }
    
    buffer[n] = '\0';
    string cmd(buffer);
    
    // Parse: "GET_PIECE filepath pieceIndex"
    stringstream ss(cmd);
    string keyword, filepath;
    int pieceIndex;
    
    ss >> keyword >> filepath >> pieceIndex;
    
    if (keyword != "GET_PIECE") {
        cerr << "[PEER] Invalid command: " << cmd << endl;
        close(clientSock);
        return;
    }
    
    cout << "[PEER] Request for piece " << pieceIndex << " of " << filepath << endl;
    
    // Open file
    ifstream file(filepath, ios::binary);
    if (!file) {
        cerr << "[PEER] Cannot open file: " << filepath << endl;
        close(clientSock);
        return;
    }
    
    // Seek to piece
    long long offset = (long long)pieceIndex * PIECE_SIZE;
    file.seekg(offset, ios::beg);
    
    if (!file) {
        cerr << "[PEER] Cannot seek to piece " << pieceIndex << endl;
        close(clientSock);
        return;
    }
    
    // Read piece
    vector<unsigned char> pieceData(PIECE_SIZE);
    file.read((char*)pieceData.data(), PIECE_SIZE);
    streamsize bytesRead = file.gcount();
    file.close();
    
    if (bytesRead <= 0) {
        cerr << "[PEER] Failed to read piece " << pieceIndex << endl;
        close(clientSock);
        return;
    }
    
    // Send piece size
    long long pieceSize = bytesRead;
    if (send(clientSock, &pieceSize, sizeof(pieceSize), 0) < 0) {
        cerr << "[PEER] Failed to send piece size" << endl;
        close(clientSock);
        return;
    }
    
    // Send piece data
    size_t totalSent = 0;
    while (totalSent < bytesRead) {
        ssize_t sent = send(clientSock, pieceData.data() + totalSent, 
                           bytesRead - totalSent, 0);
        if (sent <= 0) {
            cerr << "[PEER] Send failed at " << totalSent << "/" << bytesRead << endl;
            break;
        }
        totalSent += sent;
    }
    
    if (totalSent == bytesRead) {
        cout << "[PEER] Successfully sent piece " << pieceIndex 
             << " (" << bytesRead << " bytes)" << endl;
    }
    
    close(clientSock);
}

// Start peer server to serve file pieces
void startPeerServer(int peerPort) {
    int serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock < 0) {
        perror("[PEER] socket");
        return;
    }
    
    int opt = 1;
    setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(serverSock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(peerPort);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (::bind(serverSock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[PEER] bind");
        close(serverSock);
        return;
    }
    
    if (listen(serverSock, 10) < 0) {
        perror("[PEER] listen");
        close(serverSock);
        return;
    }
    
    cout << "[PEER] Server listening on port " << peerPort << endl;
    
    while (true) {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        
        int clientSock = accept(serverSock, (sockaddr*)&clientAddr, &clientLen);
        if (clientSock < 0) {
            perror("[PEER] accept");
            continue;
        }
        
        // Handle each request in a new thread
        thread(handlePeerRequest, clientSock).detach();
    }
    
    close(serverSock);
}