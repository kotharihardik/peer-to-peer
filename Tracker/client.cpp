// #include <arpa/inet.h>
// #include <unistd.h>
// #include <iostream>
// #include <unordered_set>
// #include <string>
// #include <cstdlib>
// #include <cstring>
// #include <sys/socket.h>
// #include <netinet/in.h>
// #include <sstream>
// #include <fstream>
// #include <thread>
// #include "header.h"

// using namespace std;

// #define BUFSIZE 4096

// // Helper: connect to first alive tracker from tracker_info.txt
// int connectToAnyTracker(const string &excludeIP, int excludePort,
//                         const string &infoFile, string &outIP, int &outPort) {
//     ifstream fin(infoFile);
//     string ip2; int port2;
//     while (fin >> ip2 >> port2) {
//         if (ip2 == excludeIP && port2 == excludePort) continue;

//         int s = socket(AF_INET, SOCK_STREAM, 0);
//         if (s < 0) continue;

//         sockaddr_in addr{};
//         addr.sin_family = AF_INET;
//         addr.sin_port = htons(port2);
//         addr.sin_addr.s_addr = inet_addr(ip2.c_str());

//         if (connect(s, (sockaddr*)&addr, sizeof(addr)) == 0) {
//             cout << "[INFO] Reconnected to tracker " << ip2 << ":" << port2 << endl;
//             outIP = ip2; outPort = port2;
//             return s;
//         }
//         close(s);
//     }
//     return -1;
// }

// int main(int argc, char *argv[]) {
//     if (argc != 3) {
//         cerr << "Usage: ./client <clientIP:clientPort> tracker_info.txt\n";
//         return 1;
//     }

//     string clientIpPort = argv[1];
//     string clientIp = clientIpPort.substr(0, clientIpPort.find(":"));
//     int clientPort = stoi(clientIpPort.substr(clientIpPort.find(":") + 1));

//     // Start peer server thread
//     thread(startPeerServer, clientPort).detach();
//     cout << "[INFO] Peer server running on port " << clientPort << endl;

//     // Read tracker info
//     ifstream fin(argv[2]);
//     if (!fin) { cerr << "Error opening tracker info file\n"; return 1; }

//     string trackerIP; int trackerPort;
//     fin >> trackerIP >> trackerPort;
//     if (!fin) { cerr << "Tracker info file empty\n"; return 1; }

//     // Connect to tracker
//     int sockfd = socket(AF_INET, SOCK_STREAM, 0);
//     if (sockfd < 0) { perror("socket"); exit(1); }

//     sockaddr_in servAddr{};
//     servAddr.sin_family = AF_INET;
//     servAddr.sin_port = htons(trackerPort);
//     servAddr.sin_addr.s_addr = inet_addr(trackerIP.c_str());

//     if (connect(sockfd, (sockaddr*)&servAddr, sizeof(servAddr)) < 0) {
//         cerr << "[WARN] Primary tracker not reachable, trying backup...\n";
//         sockfd = connectToAnyTracker(trackerIP, trackerPort, argv[2], trackerIP, trackerPort);
//         if (sockfd < 0) { cerr << "[ERROR] No trackers available\n"; return 1; }
//     }
//     cout << "[INFO] Connected to tracker at " << trackerIP << ":" << trackerPort << endl;

//     string currentUser = "";
//     bool loggedIn = false;

//     // -------- MAIN LOOP --------
//     while (true) {
//         cout << ">> ";
//         string line;
//         getline(cin, line);
        
//         if (line == "quit") {
//             break;
//         }

//         stringstream ss(line);
//         string command, next;
//         ss >> command;

//         // ===== UPLOAD FILE =====
//         if (command == "upload_file") {
//             if (!loggedIn) { 
//                 cout << "ERROR: Please login first.\n"; 
//                 continue; 
//             }
            
//             string groupId, filePath;
//             ss >> groupId >> filePath;
            
//             string uploadMsg = send_upload_to_tracker(sockfd, currentUser, groupId, filePath, clientIp, clientPort);
//             cout << uploadMsg;

//             if (uploadMsg.find("SUCCESS") != string::npos) {
//                 // Add to shared files
//                 string filename = filePath.substr(filePath.find_last_of("/\\") + 1);
//                 addSharedFile(filename, filePath);
//             }
//             continue;
//         }

//         // ===== DOWNLOAD FILE =====
//         else if (command == "download_file") {
//             if (!loggedIn) { 
//                 cout << "ERROR: Please login first.\n"; 
//                 continue; 
//             }

//             string groupId, filename, destPath;
//             ss >> groupId >> filename >> destPath;

//             string request = "download_file " + groupId + " " + filename + "\n";
//             send(sockfd, request.c_str(), request.size(), 0);

//             string reply = recvAll(sockfd);

//             cout << reply << endl;
//             stringstream rs(reply);
//             string status;
//             rs >> status;

//             if (status != "OK") {
//                 cout << reply;
//                 continue;
//             }

//             long long filesize;
//             int pieceCount;
//             string fileSha1;
//             rs >> filesize >> pieceCount >> fileSha1;

//             vector<string> pieceHashes(pieceCount);
//             for (int i = 0; i < pieceCount; i++) {
//                 rs >> pieceHashes[i];
//             }

//             vector<Seeder> seeders;
//             string token;
//             while (rs >> token && token != "END") {
//                 Seeder s;
//                 s.userId = token;
//                 string piecesStr;
//                 rs >> s.ip >> s.port >> piecesStr;

//                 stringstream ps(piecesStr);
//                 string p;
//                 while (getline(ps, p, ',')) {
//                     if (!p.empty()) s.pieces.insert(stoi(p));
//                 }
//                 seeders.push_back(s);
//             }

//             cout << "[INFO] Starting download in background thread...\n";
//             thread(downloadFileFromPeers, filename, destPath, filesize, pieceHashes, seeders, fileSha1).detach();
            
//             // After successful download, add to shared files
          

//             // CORRECT:
//             // string actualFilePath = destPath;
//             // if (actualFilePath.back() == '/') {
//             //     actualFilePath += filename;
//             // } else if (actualFilePath.find('.') == string::npos) {
//             //     actualFilePath += "/" + filename;
//             // }
//             // addSharedFile(filename, actualFilePath);
//         }

//         // ===== SHOW DOWNLOADS =====
//         else if (command == "show" && next == "downloads") {
//             cout << showDownloads();
//             continue;
//         }

//         // ===== LIST FILES =====
//         // else if (command == "list_files") {
//         //     string groupId;
//         //     ss >> groupId;
//         //     line = "list files " + groupId + "\n";
//         // }

//         // ===== STOP SHARE =====
//         else if (command == "stop" && next == "share") {
//             if (!loggedIn) {
//                 cout << "ERROR: Please login first.\n";
//                 continue;
//             }
//             line += "\n";
//         }

//         // ===== DEFAULT: SEND TO TRACKER =====
//         else {
//             if (line.back() != '\n') {
//                 line += "\n";
//             }
//         }

//         // Send command to tracker
//         if (line.back() != '\n') {
//             line += "\n";
//         }

//         ssize_t sent = send(sockfd, line.c_str(), line.size(), 0);
//         if (sent < 0) {
//             perror("send");
//             break;
//         }

//         // Receive response
//         char buffer[BUFSIZE];
//         ssize_t n = recv(sockfd, buffer, BUFSIZE - 1, 0);
        
//         if (n <= 0) {
//             cout << "[WARN] Lost connection to tracker " << trackerIP << ":" << trackerPort << endl;
//             close(sockfd);
//             sockfd = connectToAnyTracker(trackerIP, trackerPort, argv[2], trackerIP, trackerPort);
            
//             if (sockfd < 0) {
//                 cerr << "[ERROR] No trackers available, exiting." << endl;
//                 break;
//             }
            
//             cout << "[INFO] Resending last command to new tracker..." << endl;
//             send(sockfd, line.c_str(), line.size(), 0);
//             n = recv(sockfd, buffer, BUFSIZE - 1, 0);
//             if (n > 0) {
//                 buffer[n] = '\0';
//                 cout << buffer;
//             }
//             continue;
//         }

//         buffer[n] = '\0';

//         // Post-processing hook for login
//         if (command == "login" && string(buffer).find("SUCCESS") != string::npos) {
//             ss >> next;
//             currentUser = next;
//             loggedIn = true;
//             cout << buffer;
//             cout << "[INFO] Logged in as " << currentUser << endl;
//             continue;
//         }

//         cout << buffer;
//     }

//     close(sockfd);
//     return 0;
// }