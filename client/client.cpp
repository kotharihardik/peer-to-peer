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

// Forward declaration
extern void downloadFile(string trackerIP, int trackerPort, string groupId, 
                        string filename, string destPath);

// Connect to any available tracker (failover)
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

// Generic command sender (reduces redundancy)
string sendCommandToTracker(int sockfd, const string& command) {
    send(sockfd, command.c_str(), command.size(), 0);
    
    char buffer[BUFSIZE];
    ssize_t n = recv(sockfd, buffer, BUFSIZE - 1, 0);
    if (n > 0) {
        buffer[n] = '\0';
        return string(buffer);
    }
    return "ERROR: No response from tracker\n";
}

// Upload downloaded file as seeder
void uploadDownloadedFileToTracker(const string& groupId, const string& filename, 
                                   const string& filepath) {
    cout << "[AUTO-SEED] Uploading " << filename << " to tracker as seeder" << endl;
    
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
    
    string uploadResult = send_upload_to_tracker(sock, g_currentUser, groupId, 
                                                 filepath, g_clientIp, g_clientPort);
    
    cout << "[AUTO-SEED] " << uploadResult;
    close(sock);
}

// Download wrapper with auto-seed and tracking
void downloadAndSeed(string groupId, string filename, string destPath) {
    // Register download
    string key = getDownloadKey(groupId, filename);

    DownloadInfo dlInfo;
    dlInfo.groupId = groupId;
    dlInfo.filename = filename;
    dlInfo.destPath = destPath;
    dlInfo.status = DOWNLOADING;

    {
        lock_guard<mutex> lock(downloadsMutex);
        activeDownloads[key] = dlInfo;
    }

    // Perform download
    downloadFile(g_trackerIP, g_trackerPort, groupId, filename, destPath);

    // Check if stopped
    bool wasStopped = false;
    {
        lock_guard<mutex> lock(downloadsMutex);
        auto it = activeDownloads.find(key);
        if (it != activeDownloads.end() && it->second.stopRequested) {
            wasStopped = true;
            it->second.status = FAILED;
        }
    }

    if (wasStopped) {
        cout << "[STOPPED] Download cancelled: " << filename << endl;
        return;
    }

    // Mark as completed
    {
        lock_guard<mutex> lock(downloadsMutex);
        auto it = activeDownloads.find(key);
        if (it != activeDownloads.end()) {
            it->second.status = COMPLETED;
        }
    }

    // Determine actual file path
    // Determine actual file path
    string actualPath = destPath;
    struct stat st;
    if (stat(destPath.c_str(), &st) == 0 && (st.st_mode & S_IFDIR)) {
        if (destPath.back() != '/') actualPath += "/";
        actualPath += filename;  // <-- ensure file name is appended
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

        // ===== LOGIN (special handling) =====
        if (command == "login") {
            if (loggedIn) {
                cout << "ERROR: Already logged in\n";
                continue;
            }
            
            string uid, pwd;
            ss >> uid >> pwd;
            
            string response = sendCommandToTracker(sockfd, line + "\n");
            cout << response;
            
            if (response.find("SUCCESS") != string::npos) {
                g_currentUser = uid;
                loggedIn = true;
                
                // Register client with tracker
                string regMsg = "REGISTER_CLIENT " + g_currentUser + " " + 
                               g_clientIp + " " + to_string(g_clientPort) + "\n";
                string regResp = sendCommandToTracker(sockfd, regMsg);
                cout << "[INFO] " << regResp;
            }
            continue;
        }

        // ===== LOGOUT (special handling) =====
        if (command == "logout") {
            if (!loggedIn) {
                cout << "ERROR: Not logged in\n";
                continue;
            }
            
            string response = sendCommandToTracker(sockfd, line + "\n");
            cout << response;
            
            if (response.find("SUCCESS") != string::npos) {
                loggedIn = false;
                g_currentUser = "";
            }
            continue;
        }

        // ===== UPLOAD FILE (special handling) =====
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

        // ===== DOWNLOAD FILE (special handling) =====
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

        // ===== SHOW DOWNLOADS =====
        if (command == "show_downloads") {
            cout << showDownloads();
            continue;
        }

        // ===== ALL OTHER COMMANDS (generic handling) =====
        // Commands that need login check
        if (!loggedIn && (command == "create_group" || command == "join_group" || 
            command == "leave_group" || command == "list_requests" || 
            command == "accept_request" || command == "list_files" || 
            command == "stop_share")) {
            cout << "ERROR: Please login first.\n";
            continue;
        }

        // Send command directly to tracker
        string response = sendCommandToTracker(sockfd, line + "\n");
        cout << response;
    }

    close(sockfd);
    return 0;
}