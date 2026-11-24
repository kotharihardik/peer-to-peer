#pragma once
// =====================================================================
// Peer - the module 2 application: one fully decentralized peer
// =====================================================================
// A peer is at the same time:
//   - a DHT node          (stores part of the global index, routes lookups)
//   - a seeder            (serves pieces of the files it shares)
//   - a downloader        (fetches pieces from other peers)
// There is no tracker and no server in this program: every peer runs exactly
// the same code.
//
// Built by COMPOSITION, and the DECLARATION ORDER is the shutdown order in
// reverse, which matters here:
//   node       -> DhtNode         the overlay (routing + this node's index slice)
//   index      -> DhtIndex        MetadataIndex implemented on top of the overlay
//   shared     -> SharedFiles     which files I serve                  [common/]
//   uploads    -> UploadManager   share / stop sharing                 [common/]
//   peerServer -> PeerServer      serves my pieces to other peers       [common/]
//   downloads  -> DownloadManager runs downloads                        [common/]
// On exit downloads stop first (a finishing download still publishes itself
// through index and node), then the peer server, and the overlay node last.
//
// TWO PORTS PER PEER: the DHT port given on the command line, and the transfer
// port DHT + TRANSFER_PORT_OFFSET where pieces are served. Keeping them apart
// means the overlay traffic and the bulk file traffic never queue behind each
// other. It follows the same convention module 1 used for the tracker's sync
// port (client port + 1000).

#include "DhtIndex.h"
#include "DhtNode.h"
#include "DownloadManager.h"
#include "PeerServer.h"
#include "SharedFiles.h"
#include "UploadManager.h"
#include "Utils.h"

#include <string>
#include <vector>

using namespace std;

// Pieces are served on (DHT port + this)
const int TRANSFER_PORT_OFFSET = 100;

// Module 2 has no groups, so every file lives in one global namespace
const string DHT_SCOPE = "dht";

class Peer {
public:
    Peer(Address dhtAddr, Address transferAddr);

    // bootstrap.port == 0 means "no bootstrap given": start a brand new ring
    bool start(Address bootstrap);
    void run();       // read commands until "quit" / "leave" / end of input
    void leave();     // graceful departure from the ring

private:
    string executeCommand(const vector<string>& args);
    static string helpText();

    Address dhtAddr;
    Address transferAddr;

    DhtNode node;
    DhtIndex index;
    SharedFiles shared;
    UploadManager uploads;
    PeerServer peerServer;
    DownloadManager downloads;
};
