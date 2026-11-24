// Peer.cpp - command loop of a decentralized peer
#include "Peer.h"
#include "Hashing.h"

#include <iostream>

using namespace std;

// Members are created in the order declared in Peer.h
Peer::Peer(Address dht, Address transfer)
    : index(node), uploads(index, shared), peerServer(shared), downloads(index, uploads, transfer) {
    dhtAddr = dht;
    transferAddr = transfer;
}

bool Peer::start(Address bootstrap) {
    // 1. be ready to serve pieces BEFORE publishing anything into the index
    if (!peerServer.start(transferAddr.port)) {
        logLine("[ERROR] Cannot listen for piece requests on port " + to_string(transferAddr.port));
        return false;
    }
    logLine("[INFO] Serving file pieces on port " + to_string(transferAddr.port));

    // 2. be reachable for overlay messages before joining
    if (!node.start(dhtAddr, transferAddr)) {
        logLine("[ERROR] Cannot listen for DHT messages on port " + to_string(dhtAddr.port));
        return false;
    }

    // 3. either start a new ring or join an existing one through any peer
    if (bootstrap.port == 0) {
        node.createRing();
    } else if (!node.joinRing(bootstrap)) {
        return false;
    }

    // 4. start re-announcing the files I share
    index.start();
    return true;
}

void Peer::leave() {
    index.stop();      // stop re-announcing before handing the records over
    node.leaveRing();  // hand off metadata and close the ring
}

void Peer::run() {
    while (true) {
        logLine(">> ", false);
        string line;
        if (!getline(cin, line)) break;   // end of input (Ctrl-D or closed pipe)

        vector<string> args = splitWords(line);
        if (args.empty()) continue;
        if (args[0] == "quit" || args[0] == "leave") break;
        logLine(executeCommand(args));
    }
    leave();
}

string Peer::executeCommand(const vector<string>& args) {
    string cmd = args[0];

    // ---- sharing files ----
    if (cmd == "share") {
        if (args.size() != 2) return "ERROR: Usage: share <file_path>";
        return uploads.upload(DHT_SCOPE, args[1]);
    }
    if (cmd == "unshare") {
        if (args.size() != 2) return "ERROR: Usage: unshare <file_name>";
        return uploads.stopShare(DHT_SCOPE, args[1]);
    }

    // ---- finding and downloading files ----
    if (cmd == "search") {
        if (args.size() != 2) return "ERROR: Usage: search <file_name>";
        return index.search(DHT_SCOPE, args[1]);
    }
    if (cmd == "get") {
        if (args.size() != 3) return "ERROR: Usage: get <file_name> <destination_path>";
        return downloads.startDownload(DHT_SCOPE, args[1], args[2]);
    }
    if (cmd == "downloads") return downloads.showDownloads();

    // ---- looking at the overlay ----
    if (cmd == "info") return node.getRouting().describe();
    if (cmd == "ring") return node.walkRing();
    if (cmd == "index") return node.getIndex().describe();
    if (cmd == "stats") return node.describeStats();

    // Show which node a name maps to without needing the file to exist
    if (cmd == "key") {
        if (args.size() != 2) return "ERROR: Usage: key <file_name>";
        unsigned int k = fileKey(DHT_SCOPE, args[1]);
        int hops = 0;
        vector<string> trace;
        NodeInfo owner = node.lookup(k, hops, trace);
        string out = args[1] + " hashes to key " + to_string(k);
        for (size_t i = 0; i < trace.size(); i++) out += "\n  " + trace[i];
        if (owner.empty()) return out + "\n  lookup failed";
        return out + "\n  responsible node: " + owner.toString() + "  (" + to_string(hops) + " hop(s))";
    }

    if (cmd == "help") return helpText();
    return "ERROR: unknown command '" + cmd + "' (type 'help')";
}

string Peer::helpText() {
    return "Commands:\n"
           "  share <file_path>              hash the file, publish its metadata into the DHT, start seeding\n"
           "  unshare <file_name>            remove it from the DHT and stop seeding\n"
           "  search <file_name>             DHT lookup: shows the hops and which peers hold the file\n"
           "  get <file_name> <dest>         look up and download from the discovered peers\n"
           "  downloads                      progress of my downloads ([D] running, [C] done, [F] failed)\n"
           "  key <file_name>                which key a name maps to, and which node is responsible\n"
           "  info                           my node id, predecessor, successors, finger table\n"
           "  ring                           walk the successor pointers around the whole ring\n"
           "  index                          the metadata slice stored on THIS node\n"
           "  stats                          message counts, lookup count, average hops and latency\n"
           "  help                           this list\n"
           "  leave / quit                   hand my metadata to my successor, then exit";
}
