#pragma once
// =====================================================================
// Client - the client application (every client is downloader AND seeder)
// =====================================================================
// Built by COMPOSITION - the Client simply owns one object of each part:
//   tracker    -> TrackerClient   (the connection to the tracker, with failover)
//   index      -> TrackerIndex    ("who has this file?" answered by the tracker)
//   shared     -> SharedFiles     (which files I share)
//   uploads    -> UploadManager   (upload_file, stop_share)          [common/]
//   peerServer -> PeerServer      (sends my pieces to other clients) [common/]
//   downloads  -> DownloadManager (download_file, show_downloads)    [common/]
//
// Member order matters: members are destroyed in reverse order, so on exit
// downloads stop first, then the peer server, and the tracker link goes last.

#include "DownloadManager.h"
#include "PeerServer.h"
#include "SharedFiles.h"
#include "TrackerClient.h"
#include "TrackerIndex.h"
#include "UploadManager.h"
#include "Utils.h"

#include <string>
#include <vector>

using namespace std;

class Client {
public:
    Client(Address self, vector<Address> trackers);

    bool start();   // start the peer server + connect to a tracker
    void run();     // read commands until "quit" or end of input

private:
    string executeCommand(const vector<string>& args, const string& line);
    string login(const vector<string>& args, const string& line);

    Address self;
    TrackerClient tracker;
    TrackerIndex index;
    SharedFiles shared;
    UploadManager uploads;
    PeerServer peerServer;
    DownloadManager downloads;
    string currentUser;   // empty = not logged in (only used by the command thread)
};
