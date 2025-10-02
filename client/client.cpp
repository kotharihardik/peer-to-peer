#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <string>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sstream>
#include <fstream>
#include <thread>
#include "header.h"
#include <sys/stat.h>


using namespace std;

#define BUFSIZE 4096

// Global tracker connection info
string g_trackerIP;
int g_trackerPort;
string g_trackerInfoFile;
string g_currentUser;
string g_clientIp;
int g_clientPort;

// Forward declarations
extern void downloadFile(string trackerIP, int trackerPort, string groupId, 
                        string filename, string destPath);

// Connect to any available tracker
int connectToAnyTracker(const string &excludeIP, int excludePort,
                        const string &infoFile, string &outIP, int &outPort) {
    ifstream fin(infoFile);
    string ip2; 
    int port2;
    
    while (fin >> ip2 >> port2) {
        if (ip2 == excludeIP && port2 == excludePort) continue;

        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) continue;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port2);
        addr.sin_addr.s_addr = inet_addr(ip2.c_str());

        if (connect(s, (sockaddr*)&addr, sizeof(addr)) == 0) {
            cout << "[INFO] Reconnected to tracker " << ip2 << ":" << port2 << endl;
            outIP = ip2; 
            outPort = port2;
            return s;
        }
        close(s);
    }
    return -1;
}

// Upload downloaded file as seeder
void uploadDownloadedFileToTracker(const string& groupId, const string& filename, 
                                   const string& filepath) {
    
    cout << "[AUTO-SEED] Uploading " << filename << " to tracker as seeder" << endl;
    
    // Connect to tracker
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        cerr << "[ERROR] Cannot create socket for auto-seed" << endl;
        return;
    }
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(g_trackerPort);
    addr.sin_addr.s_addr = inet_addr(g_trackerIP.c_str());
    
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        cerr << "[ERROR] Cannot connect to tracker for auto-seed" << endl;
        close(sock);
        return;
    }
    
    // Send upload metadata
    string uploadResult = send_upload_to_tracker(sock, g_currentUser, groupId, 
                                                 filepath, g_clientIp, g_clientPort);
    
    cout << "[AUTO-SEED] " << uploadResult;
    close(sock);
}

// Download wrapper with auto-seed
void downloadAndSeed(string groupId, string filename, string destPath) {
    // Perform download
    downloadFile(g_trackerIP, g_trackerPort, groupId, filename, destPath);
    
    // After successful download, upload as seeder
    // Determine actual file path
    string actualPath = destPath;
    struct stat info;
    if (stat(destPath.c_str(), &info) == 0 && (info.st_mode & S_IFDIR)) {
        if (destPath.back() != '/') actualPath += "/";
        actualPath += filename;
    }
    
    // Wait a bit to ensure file is fully written
    this_thread::sleep_for(chrono::seconds(1));
    
    // Upload to tracker as new seeder
    uploadDownloadedFileToTracker(groupId, filename, actualPath);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        cerr << "Usage: ./client <clientIP:clientPort> tracker_info.txt\n";
        return 1;
    }

    string clientIpPort = argv[1];
    g_clientIp = clientIpPort.substr(0, clientIpPort.find(":"));
    g_clientPort = stoi(clientIpPort.substr(clientIpPort.find(":") + 1));
    g_trackerInfoFile = argv[2];

    // Start peer server thread
    thread(startPeerServer, g_clientPort).detach();
    cout << "[INFO] Peer server running on port " << g_clientPort << endl;

    // Read tracker info
    ifstream fin(argv[2]);
    if (!fin) { 
        cerr << "Error opening tracker info file\n"; 
        return 1; 
    }

    fin >> g_trackerIP >> g_trackerPort;
    if (!fin) { 
        cerr << "Tracker info file empty\n"; 
        return 1; 
    }
    fin.close();

    // Connect to tracker
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { 
        perror("socket"); 
        exit(1); 
    }

    sockaddr_in servAddr{};
    servAddr.sin_family = AF_INET;
    servAddr.sin_port = htons(g_trackerPort);
    servAddr.sin_addr.s_addr = inet_addr(g_trackerIP.c_str());

    if (connect(sockfd, (sockaddr*)&servAddr, sizeof(servAddr)) < 0) {
        cerr << "[WARN] Primary tracker not reachable, trying backup...\n";
        sockfd = connectToAnyTracker(g_trackerIP, g_trackerPort, argv[2], 
                                     g_trackerIP, g_trackerPort);
        if (sockfd < 0) { 
            cerr << "[ERROR] No trackers available\n"; 
            return 1; 
        }
    }
    cout << "[INFO] Connected to tracker at " << g_trackerIP << ":" << g_trackerPort << endl;

    bool loggedIn = false;

    // Main command loop
    while (true) {
        cout << ">> ";
        string line;
        getline(cin, line);
        
        if (line.empty()) continue;
        if (line == "quit") break;

        stringstream ss(line);
        string command;
        ss >> command;

        // ===== CREATE USER =====
        if (command == "create_user") {
            string uid, pwd;
            ss >> uid >> pwd;
            
            string request = "create_user " + uid + " " + pwd + "\n";
            send(sockfd, request.c_str(), request.size(), 0);
            
            char buffer[BUFSIZE];
            ssize_t n = recv(sockfd, buffer, BUFSIZE - 1, 0);
            if (n > 0) {
                buffer[n] = '\0';
                cout << buffer;
            }
            continue;
        }

        // ===== LOGIN =====
        if (command == "login") {
            if (loggedIn) {
                cout << "ERROR: Already logged in\n";
                continue;
            }
            
            string uid, pwd;
            ss >> uid >> pwd;
            
            string request = "login " + uid + " " + pwd + "\n";
            send(sockfd, request.c_str(), request.size(), 0);
            
            char buffer[BUFSIZE];
            ssize_t n = recv(sockfd, buffer, BUFSIZE - 1, 0);
            if (n > 0) {
                buffer[n] = '\0';
                string response(buffer);
                cout << response;
                
                if (response.find("SUCCESS") != string::npos) {
                    g_currentUser = uid;
                    loggedIn = true;
                    
                    // Register client with tracker
                    string regMsg = "REGISTER_CLIENT " + g_currentUser + " " + 
                                   g_clientIp + " " + to_string(g_clientPort) + "\n";
                    send(sockfd, regMsg.c_str(), regMsg.size(), 0);
                    
                    char regBuf[BUFSIZE];
                    ssize_t regN = recv(sockfd, regBuf, BUFSIZE - 1, 0);
                    if (regN > 0) {
                        regBuf[regN] = '\0';
                        cout << "[INFO] " << regBuf;
                    }
                }
            }
            continue;
        }

        // ===== LOGOUT =====
        if (command == "logout") {
            if (!loggedIn) {
                cout << "ERROR: Not logged in\n";
                continue;
            }
            
            string request = "logout\n";
            send(sockfd, request.c_str(), request.size(), 0);
            
            char buffer[BUFSIZE];
            ssize_t n = recv(sockfd, buffer, BUFSIZE - 1, 0);
            if (n > 0) {
                buffer[n] = '\0';
                cout << buffer;
                
                if (string(buffer).find("SUCCESS") != string::npos) {
                    loggedIn = false;
                    g_currentUser = "";
                }
            }
            continue;
        }

        // ===== UPLOAD FILE =====
        if (command == "upload_file") {
            if (!loggedIn) { 
                cout << "ERROR: Please login first.\n"; 
                continue; 
            }
            
            string groupId, filePath;
            ss >> groupId >> filePath;
            
            string uploadMsg = send_upload_to_tracker(sockfd, g_currentUser, groupId, 
                                                     filePath, g_clientIp, g_clientPort);
            cout << uploadMsg;
            continue;
        }

        // ===== DOWNLOAD FILE =====
        if (command == "download_file") {
            if (!loggedIn) { 
                cout << "ERROR: Please login first.\n"; 
                continue; 
            }

            string groupId, filename, destPath;
            ss >> groupId >> filename >> destPath;

            // Start download in background with auto-seeding
            thread(downloadAndSeed, groupId, filename, destPath).detach();
            cout << "[INFO] Download started in background\n";
            continue;
        }

        // ===== CREATE GROUP =====
        if (command == "create_group") {
            if (!loggedIn) {
                cout << "ERROR: Please login first.\n";
                continue;
            }
            
            string groupId;
            ss >> groupId;
            
            string request = "create_group " + groupId + "\n";
            send(sockfd, request.c_str(), request.size(), 0);
            
            char buffer[BUFSIZE];
            ssize_t n = recv(sockfd, buffer, BUFSIZE - 1, 0);
            if (n > 0) {
                buffer[n] = '\0';
                cout << buffer;
            }
            continue;
        }

        // ===== JOIN GROUP =====
        if (command == "join_group") {
            if (!loggedIn) {
                cout << "ERROR: Please login first.\n";
                continue;
            }
            
            string groupId;
            ss >> groupId;
            
            string request = "join group " + groupId + "\n";
            send(sockfd, request.c_str(), request.size(), 0);
            
            char buffer[BUFSIZE];
            ssize_t n = recv(sockfd, buffer, BUFSIZE - 1, 0);
            if (n > 0) {
                buffer[n] = '\0';
                cout << buffer;
            }
            continue;
        }

        // ===== LEAVE GROUP =====
        if (command == "leave_group") {
            if (!loggedIn) {
                cout << "ERROR: Please login first.\n";
                continue;
            }
            
            string groupId;
            ss >> groupId;
            
            string request = "leave group " + groupId + "\n";
            send(sockfd, request.c_str(), request.size(), 0);
            
            char buffer[BUFSIZE];
            ssize_t n = recv(sockfd, buffer, BUFSIZE - 1, 0);
            if (n > 0) {
                buffer[n] = '\0';
                cout << buffer;
            }
            continue;
        }

        // ===== LIST GROUPS =====
        if (command == "list_groups") {
            string request = "list groups\n";
            send(sockfd, request.c_str(), request.size(), 0);
            
            char buffer[BUFSIZE];
            ssize_t n = recv(sockfd, buffer, BUFSIZE - 1, 0);
            if (n > 0) {
                buffer[n] = '\0';
                cout << buffer;
            }
            continue;
        }

        // ===== LIST FILES =====
        if (command == "list_files") {
            if (!loggedIn) {
                cout << "ERROR: Please login first.\n";
                continue;
            }
            
            string groupId;
            ss >> groupId;
            
            string request = "list_files " + groupId + "\n";
            send(sockfd, request.c_str(), request.size(), 0);
            
            char buffer[BUFSIZE];
            ssize_t n = recv(sockfd, buffer, BUFSIZE - 1, 0);
            if (n > 0) {
                buffer[n] = '\0';
                cout << buffer;
            }
            continue;
        }

        // ===== LIST REQUESTS =====
        if (command == "list_requests") {
            if (!loggedIn) {
                cout << "ERROR: Please login first.\n";
                continue;
            }
            
            string groupId;
            ss >> groupId;
            
            string request = "list requests " + groupId + "\n";
            send(sockfd, request.c_str(), request.size(), 0);
            
            char buffer[BUFSIZE];
            ssize_t n = recv(sockfd, buffer, BUFSIZE - 1, 0);
            if (n > 0) {
                buffer[n] = '\0';
                cout << buffer;
            }
            continue;
        }

        // ===== ACCEPT REQUEST =====
        if (command == "accept_request") {
            if (!loggedIn) {
                cout << "ERROR: Please login first.\n";
                continue;
            }
            
            string groupId, userId;
            ss >> groupId >> userId;
            
            string request = "accept request " + groupId + " " + userId + "\n";
            send(sockfd, request.c_str(), request.size(), 0);
            
            char buffer[BUFSIZE];
            ssize_t n = recv(sockfd, buffer, BUFSIZE - 1, 0);
            if (n > 0) {
                buffer[n] = '\0';
                cout << buffer;
            }
            continue;
        }

        // ===== STOP SHARE =====
        if (command == "stop_share") {
            if (!loggedIn) {
                cout << "ERROR: Please login first.\n";
                continue;
            }
            
            string groupId, filename;
            ss >> groupId >> filename;
            
            string request = "stop share " + groupId + " " + filename + "\n";
            send(sockfd, request.c_str(), request.size(), 0);
            
            char buffer[BUFSIZE];
            ssize_t n = recv(sockfd, buffer, BUFSIZE - 1, 0);
            if (n > 0) {
                buffer[n] = '\0';
                cout << buffer;
            }
            continue;
        }

        // ===== UNKNOWN COMMAND =====
        cout << "ERROR: Unknown command. Available commands:\n"
             << "  create_user <user_id> <password>\n"
             << "  login <user_id> <password>\n"
             << "  logout\n"
             << "  create_group <group_id>\n"
             << "  join_group <group_id>\n"
             << "  leave_group <group_id>\n"
             << "  list_groups\n"
             << "  list_requests <group_id>\n"
             << "  accept_request <group_id> <user_id>\n"
             << "  upload_file <group_id> <file_path>\n"
             << "  list_files <group_id>\n"
             << "  download_file <group_id> <file_name> <destination_path>\n"
             << "  stop_share <group_id> <file_name>\n"
             << "  quit\n";
    }

    close(sockfd);
    return 0;
}