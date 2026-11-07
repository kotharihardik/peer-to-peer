#include <iostream>     // cout, cerr
#include <fstream>      // ifstream
#include <vector>       // vector
#include <string>       // string
#include <utility>      // pair
#include <cstdlib>      // exit, stoi
#include <cstring>      // memset, strerror
#include <sys/socket.h> // socket, bind, listen, accept, recv, send
#include <netinet/in.h> // sockaddr_in, htons
#include <arpa/inet.h>  // inet_addr
#include <unistd.h>     // close
#include <thread>       // for std::thread
#include <algorithm>    // for remove

#include "header.h"



// externs from sync.cpp
extern void syncListener(int syncPort);
extern void broadcastCommandToTrackers(const std::string &cmdLine);
extern bool requestFullStateFromPeerAndApply(const std::string &peerIP, int peerSyncPort);

using namespace std;

#define BACKLOG 10
#define BUFSIZE 1024


extern int sockfd;      // tell compiler it's defined elsewhere
extern int syncSockfd;  // tell compiler it's defined elsewhere

// Signal Handler
void handleSigint(int sig) {
    cout << "\nShutting down tracker..." << endl;

    if (sockfd > 0) close(sockfd);
    if (syncSockfd > 0) close(syncSockfd);
    exit(0);
}

void clientHandler(int clientSock, string clientIP, int clientPort) {
    while (true) {
        // Read complete command (ends with '\n')
        string cmd;
        char buffer[8192];
        bool foundNewline = false;
        
        while (!foundNewline) {
            ssize_t n = recv(clientSock, buffer, sizeof(buffer)-1, 0);
            if (n <= 0) {
                cerr << "[INFO] Client disconnected: " << clientIP << ":" << clientPort << endl;
                
                {
                    lock_guard<mutex> lock(metadataMutex);
                    for (auto &kv : users) {
                        if (kv.second.socketFd == clientSock) {
                            kv.second.loggedIn = false;
                            kv.second.socketFd = -1;
                            cerr << "[INFO] Marked user " << kv.first << " as logged out\n";
                            break;
                        }
                    }
                }
                close(clientSock);
                return;
            }
            
            buffer[n] = '\0';
            cmd += string(buffer, n);
            
            // Check if we have complete command
            if (cmd.find('\n') != string::npos) {
                foundNewline = true;
            }
            
            // Log progress for large commands
            if (cmd.size() % 8192 == 0) {
                cerr << "[RECV] Received " << cmd.size() << " bytes so far..." << endl;
            }
        }
        
        // Remove newline/carriage returns
        cmd.erase(remove(cmd.begin(), cmd.end(), '\n'), cmd.end());
        cmd.erase(remove(cmd.begin(), cmd.end(), '\r'), cmd.end());
        
        cerr << "[RECV] Complete command: " << cmd.size() << " bytes" << endl;
        
        string reply;
        
        if (cmd.rfind("REGISTER_CLIENT", 0) == 0) {
            string keyword, uid, peerIP;
            int peerPort;
            stringstream ss(cmd);
            ss >> keyword >> uid >> peerIP >> peerPort;
            
            {
                lock_guard<mutex> lock(metadataMutex);
                if (users.count(uid)) {
                    users[uid].ip = peerIP;
                    users[uid].port = peerPort;
                    users[uid].socketFd = clientSock;
                    users[uid].loggedIn = true;
                }
            }
            
            reply = "SUCCESS: Client registered with tracker\n";
            send(clientSock, reply.c_str(), reply.size(), 0);
        }
        else if (cmd.rfind("upload_file", 0) == 0) {
            cout << "receive upload meta....." << endl;
            reply = handle_upload_file(cmd);
            broadcastCommandToTrackers(cmd);
            send(clientSock, reply.c_str(), reply.size(), 0);
        }
       else if (cmd.rfind("download_file", 0) == 0) {
        cerr << "[TRACKER] Received download_file command: " << cmd << endl;
        handleDownloadCommand(clientSock, cmd);
        cerr << "[TRACKER] Completed download_file command handling" << endl;
    }
        else {
            reply = handleCommand(cmd, clientIP, clientPort, clientSock);
            broadcastCommandToTrackers(cmd);
            send(clientSock, reply.c_str(), reply.size(), 0);
        }
    }
}

//  Main
int main(int argc, char *argv[]) {
    signal(SIGINT, handleSigint);
    signal(SIGTERM, handleSigint);

    if (argc != 3) {
        cerr << "Usage: ./tracker tracker_info.txt tracker_no\n";
        return 1;
    }

    string info_file= argv[1];
    myTrackerNo =stoi(argv[2]);

    ifstream fin(info_file);
    string ip; int port;
    while (fin >> ip >> port)
    {
         trackersInfo.push_back({ip, port});
    }
    fin.close();

    // <-- Add the check here
    cerr << "Trackers loaded: " << trackersInfo.size() << endl;
    for (int i = 0; i < trackersInfo.size(); i++) {
        cerr << i << ": " << trackersInfo[i].first << ":" << trackersInfo[i].second << endl;
    }

    // Now safely access myTrackerNo
    if (myTrackerNo < 1 || myTrackerNo > trackersInfo.size()) {
        cerr << "Invalid tracker number: " << myTrackerNo << endl;
        return 1;
    }



    myIP =trackersInfo[myTrackerNo-1].first;
    myPort= trackersInfo[myTrackerNo-1].second;

    // Start sync listener
    thread(syncListener, myPort + 1000).detach(); // or explicit sync port from config

    // ---------------- Request full state from peer ----------------
    int peerNo =(myTrackerNo == 1 ? 2 : 1);
    string peerIP = trackersInfo[peerNo-1].first;
    int peerSyncPort = trackersInfo[peerNo-1].second + 1000;
    

    cerr << "[SYNC] Attempting to pull state from peer " << peerIP << ":" << peerSyncPort << endl;
    if (!requestFullStateFromPeerAndApply(peerIP, peerSyncPort)) {
        cerr << "[SYNC] Peer not available, continuing with local state." << endl;
    }


    // ---------------- Client listener ----------------
    sockfd =socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
     { 
        perror("socket");
         exit(1); 
     }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET,SO_REUSEADDR, &opt,sizeof(opt));
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &opt,sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(myPort);
    addr.sin_addr.s_addr = inet_addr(myIP.c_str());

    if (::bind(sockfd, (sockaddr*)&addr, sizeof(addr)) < 0) { 
        perror("bind"); 
        exit(1); 
    }
    if (listen(sockfd, BACKLOG) < 0){ 
        perror("listen"); 
        exit(1);
    }

    cout << "Tracker running at " << myIP << ":" << myPort << endl;

   while (true) {
    sockaddr_in clientAddr{};
    socklen_t clientLen = sizeof(clientAddr);

    int clientSock = accept(sockfd, (sockaddr*)&clientAddr, &clientLen);
    if (clientSock < 0) { 
        perror("accept"); 
        continue;
    }

    char clientIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, sizeof(clientIP));
    int clientPort = ntohs(clientAddr.sin_port);

    cerr << "[INFO] New client connection: " << clientIP << ":" << clientPort << endl;

    // Pass clientSock, clientIP, and clientPort to handler
    thread(clientHandler, clientSock, string(clientIP), clientPort).detach();
}



    close(sockfd);
    return 0;
}

