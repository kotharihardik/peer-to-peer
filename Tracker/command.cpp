#include "header.h"
#include <sstream>
#include <iostream>
#include <mutex>

mutex user_mutex;
mutex group_mutex; 



// User Commands 
string createUser(const string &uid, const string &pwd, const string &clientIP, int clientPort) {
    
    lock_guard<mutex> lock(user_mutex);  //lock automatically release when methos goes out of scope
     if(users.count(uid))
    {
         return "User already exists\n";
    }    
    users[uid] = {uid, pwd, false, clientIP, clientPort , -1};

    // users[uid] = User{uid, pwd, false, clientIP, clientPort, -1};

    return "SUCCESS: User created\n";
}

string loginUser(const string &uid, const string &pwd, const string &clientIP, int clientPort , int client_sock) 
{
     if(!users.count(uid))
    {
       return "ERROR: User does not exist\n";
    }   
     if(users[uid].password != pwd) return "ERROR: Wrong password\n";
     if(users[uid].loggedIn) return "User already logged in\n";

    users[uid].loggedIn = true;
    users[uid].socketFd = client_sock;

    {
        lock_guard<mutex> lg(sessionMutex);
        activeSessions[client_sock] = uid;
    }


    return "SUCCESS: Logged in\n";
}

string logoutUser(const string &uid , int client_soc)
 {
     if(!users.count(uid)) return "ERROR: User does not exist\n";
     if(!users[uid].loggedIn)
    {
        return "ERROR: User not logged in\n";
    }    

    users[uid].loggedIn = false;

    users[uid].socketFd = -1;
    {
        lock_guard<mutex> lg(sessionMutex);
        activeSessions.erase(client_soc);
    }
    return "SUCCESS: Logged out\n";
}

//  group commands 
string createGroup(const string &uid, const string &groupId)
 {
 
    cout << "inside the create group() fun" << endl;
    lock_guard<mutex> lock(group_mutex);
     if(!users.count(uid) || !users[uid].loggedIn) 
    {
        return "ERROR: User not logged in\n";
    }    
     if(groups.count(groupId))
    {
        return " Group already exists\n";
    }    

    groups[groupId] = {groupId, uid, {uid}, {}};
    return "SUCCESS: Group created\n";
}

string joinGroup(const string &uid, const string &groupId)
 {
    if(!users.count(uid) || !users[uid].loggedIn)
    {
         return "ERROR: User not logged in\n";
    }
     if(!groups.count(groupId)) return "ERROR: Group does not exist\n";
    if(groups[groupId].members.count(uid))
    {
        return "Already a member\n";
    }     
     if(groups[groupId].pendingRequests.count(uid)) return " Request already sent\n";

    groups[groupId].pendingRequests.insert(uid);
    return "SUCCESS: Join request sent\n";
}

string leaveGroup(const string &uid,const string &groupId)
 {
    if(!users.count(uid) || !users[uid].loggedIn) return "ERROR: User not logged in\n";
    if(!groups.count(groupId))
    {
         return "ERROR: Group does not exist\n";
    }     
     if(!groups[groupId].members.count(uid))
    {
         return "ERROR: Not a member\n";
    }     
    
    // if the owner is leaving
    if(groups[groupId].owner== uid) {
        // Remove the owner first
        groups[groupId].members.erase(uid);

        if(groups[groupId].members.empty())
         {
            // No members left, delete the group
            groups.erase(groupId);
            return "SUCCESS: no member left, group deleted\n";
        } else {
            // Pick the first member as new owner
            string newOwner = *groups[groupId].members.begin();
            groups[groupId].owner = newOwner;
            return "SUCCESS: Owner left, new owner = " + newOwner + "\n";
        }
    }

    groups[groupId].members.erase(uid);
    return "SUCCESS: Left group\n";
}

string listGroups() {
    if(groups.empty())
    {
         return "No groups available\n";
    }   
    string result;
    for (auto &p : groups)
     {
        result += p.first + "\n";
    }
    return result;
}

string listRequests(const string &uid, const string &groupId)
{
     if(!users.count(uid) || !users[uid].loggedIn) return "ERROR: User not logged in\n";
     if(!groups.count(groupId)) return "ERROR: Group does not exist\n";
     if(groups[groupId].owner!= uid) 
     {
        return "ERROR: Only owner can list requests\n";
     }   
     if(groups[groupId].pendingRequests.empty()) return "No pending requests\n";

    string result;
    for (auto &r : groups[groupId].pendingRequests)
    {
         result += r + "\n";
    }     
    return result;
}

string acceptRequest(const string &uid, const string &groupId, const string &reqUserId)
 {
     if(!users.count(uid)|| !users[uid].loggedIn) return "ERROR: User not logged in\n";
     if(!groups.count(groupId))
     {
         return "ERROR: Group does not exist\n";
     }    
     if(groups[groupId].owner !=uid) return "Only owner can accept requests\n";
     if(!groups[groupId].pendingRequests.count(reqUserId)) return "No such pending request\n";

    groups[groupId].pendingRequests.erase(reqUserId);
    groups[groupId].members.insert(reqUserId);
    return "SUCCESS: Request accepted\n";
}

//  Handle Command 

mutex data_mutex;

string handleCommand(const string &cmdLine, const string &clientIP, int clientPort, int client_sock) {

    lock_guard<mutex> lock(data_mutex); 

    stringstream ss(cmdLine);
    string cmd, sub, arg1, arg2;
    ss >> cmd;

    if(cmd == "create_user") {
            ss >> arg1 >> arg2;
            return createUser(arg1, arg2, clientIP, clientPort);
    }

   else  if(cmd == "create_group") {

            cout << "inside the group function......";
            ss >> arg1;
            // Identify which user is creating the group (match by client IP + port)
            string uid = "";
            {
                cout << "[DEBUG] Active sessions: ";
                for (const auto &p : activeSessions) {
                    cout << p.first << " -> " << p.second << ", ";
                }
                cout << endl;
                lock_guard<mutex> lg(sessionMutex);
                if (activeSessions.count(client_sock)) {
                    uid = activeSessions[client_sock];
                }
            }
            if (uid.empty() || !users[uid].loggedIn) {
                return "ERROR: User not logged in\n";
            }
 
            return createGroup(uid, arg1);
        }
    else if (cmd == "login") {
        ss >> arg1 >> arg2; // uid, password
        string reply = loginUser(arg1, arg2, clientIP, clientPort , client_sock);
        if (reply.find("SUCCESS") != string::npos) {
            lock_guard<mutex> lg(sessionMutex);
            activeSessions[client_sock] = arg1;   // <-- BIND socket to user here
            users[arg1].socketFd = client_sock;
            // users[arg1].ip = clientIP;
            // users[arg1].port = clientPort;
            users[arg1].loggedIn = true;
        }
        return reply;
    }


    else  if(cmd == "logout") {
        // Identify user by IP+port
        string uid = "";
        {
            lock_guard<mutex> lg(sessionMutex);
            if (activeSessions.count(client_sock)) {
                uid = activeSessions[client_sock];
            }
        }
        if (uid.empty() || !users[uid].loggedIn) {
            return "ERROR: User not logged in\n";
        }

        return logoutUser(uid , client_sock);
    }

    else  if(cmd == "join")
     {
        ss >> sub >> arg1; // sub should be "group", arg1 = groupId
         if(sub != "group") 
         {
            return "ERROR: Unknown command\n";
         }   
        string uid = "";
        {
            lock_guard<mutex> lg(sessionMutex);
            if (activeSessions.count(client_sock)) {
                uid = activeSessions[client_sock];
            }
        }
        if (uid.empty() || !users[uid].loggedIn) {
            return "ERROR: User not logged in\n";
        }
        return joinGroup(uid, arg1);
    }

    else  if(cmd == "leave")
     {
        ss >> sub >> arg1; // sub = "group", arg1 = groupId
        if(sub != "group") return "ERROR: Unknown command\n";
        string uid = "";
        for (auto &p : users)
         {
             if(p.second.loggedIn &&p.second.ip ==clientIP && p.second.port == clientPort)
              {
                uid = p.first;
                break;
            }
        }

         if(uid.empty()) return "ERROR: User not logged in\n";
        return leaveGroup(uid, arg1);
    }

    else  if(cmd == "list") 
    {
        ss >> sub;
         if(sub == "groups")
         {
             return listGroups();
         }    
        else  if(sub == "requests") {
            ss >> arg1; // groupId
        string uid = "";
        {
            lock_guard<mutex> lg(sessionMutex);
            if (activeSessions.count(client_sock)) {
                uid = activeSessions[client_sock];
            }
        }
        if (uid.empty() || !users[uid].loggedIn) {
            return "ERROR: User not logged in\n";
        }
            return listRequests(uid, arg1);
        }
    }

    else  if(cmd == "accept") {
        ss >> sub >> arg1 >> arg2; // sub="request", arg1=groupId, arg2=userId
         if(sub != "request")
         {
             return "ERROR: Unknown command\n";
         }
        string uid = "";
        {
            lock_guard<mutex> lg(sessionMutex);
            if (activeSessions.count(client_sock)) {
                uid = activeSessions[client_sock];
            }
        }
        if (uid.empty() || !users[uid].loggedIn) {
            return "ERROR: User not logged in\n";
        }
        return acceptRequest(uid, arg1, arg2);
    }

    // inside handleCommand on tracker
    // else if (cmd == "download") {
    //     ss >> sub >> arg1 >> arg2; // sub should be "file", arg1=groupId, arg2=filename
    //     if (sub != "file") return "ERROR: Unknown command for download\n";

    //     // verify user logged in (found same as your other functions)
    //     string uid = "";
    //     for (auto &p : users) {
    //         if (p.second.loggedIn && p.second.ip == clientIP && p.second.port == clientPort) {
    //             uid = p.first;
    //             break;
    //         }
    //     }
    //     if (uid.empty()) return "ERROR: User not logged in\n";

    //     string metaStr;
    //     string status = getFileMetadataForDownload(arg1, arg2, metaStr);
    //     if (status != "SUCCESS") return status;
    //     return string("META ") + metaStr + "\n";
    // }

       else if (cmd == "list_files") {
        string groupId;
        ss >> groupId;
        
        lock_guard<mutex> lock(metadataMutex);
        
        if (!groups.count(groupId)) {
            return "ERROR: Group does not exist\n";
        }
        
        auto it = groupFiles.find(groupId);
        if (it == groupFiles.end() || it->second.empty()) {
            return "No files in this group\n";
        }
        
        ostringstream oss;
        oss << "Files in group " << groupId << ":\n";
        
        for (const auto &pair : it->second) {
            const FileMeta &meta = pair.second;
            oss << "  " << meta.filename << " (" << meta.filesize << " bytes, " 
                << meta.seeders.size() << " seeder(s))\n";
        }
        
        return oss.str();
       }
   else if (cmd == "download_file") {
        string groupId, filename;
        ss >> groupId >> filename;

        lock_guard<mutex> lock(metadataMutex);

        if (!groups.count(groupId)) {
            return "ERROR: Group does not exist\n";
        }

        if (!groupFiles.count(groupId) || !groupFiles[groupId].count(filename)) {
            return "ERROR: File not found\n";
        }

        FileMeta &meta = groupFiles[groupId][filename];

        if (meta.seeders.empty()) {
            return "ERROR: No seeders available\n";
        }

        // Build plain response
        ostringstream out;
        out << "OK "
            << meta.filesize << " "
            << meta.pieceCount << " "
            << meta.fileSha1 << "\n";

        // Piece SHA1 hashes
        for (int i = 0; i < meta.pieceCount; i++) {
            out << meta.pieceSha1[i] << "\n";
        }

        // Seeder info
        for (auto &seederPair : meta.seeders) {
            string uid = seederPair.first;
            if (!users.count(uid) || !users[uid].loggedIn) continue;

            out << uid << " "
                << users[uid].ip << " "
                << users[uid].port << " ";

            // List of pieces this seeder has
            bool first = true;
            for (int piece : seederPair.second.pieces) {
                if (!first) out << ",";
                out << piece;
                first = false;
            }
            out << "\n";
        }

        out << "END\n";
        return out.str();
    }

    else if (cmd == "stop" && arg1 == "share") {
        string groupId, filename;
        ss >> groupId >> filename;
        
        // Get current user from session
        string uid;
        {
            lock_guard<mutex> lg(sessionMutex);
            if (activeSessions.count(client_sock)) {
                uid = activeSessions[client_sock];
            } else {
                return "ERROR: Not logged in\n";
            }
        }
        
        lock_guard<mutex> lock(metadataMutex);
        
        if (!groups.count(groupId)) return "ERROR: Group does not exist\n";
        if (!users.count(uid) || !users[uid].loggedIn) return "ERROR: User not logged in\n";
        if (!groupFiles.count(groupId) || !groupFiles[groupId].count(filename))
            return "ERROR: File not found\n";
        
        FileMeta &meta = groupFiles[groupId][filename];
        
        if (!meta.seeders.count(uid))
            return "ERROR: You are not sharing this file\n";
        
        // Remove this user from seeder list
        meta.seeders.erase(uid);
        
        return "SUCCESS: Stopped sharing " + filename + "\n";
    }

    return "ERROR: Unknown command ---last\n";
}






