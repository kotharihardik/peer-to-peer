#pragma once
// =====================================================================
// NodeInfo - one peer as seen by the overlay: its id and where to reach it
// =====================================================================
// IMPORTANT distinction to make in an interview: this is a LOGICAL neighbour.
// "Successor" does not mean physically nearby, it means next clockwise in the
// id space. Two successors can be on opposite sides of the world; every link
// in the overlay is an ordinary TCP connection made when it is needed.

#include "Utils.h"

#include <string>
#include <vector>

using namespace std;

struct NodeInfo {
    unsigned int id;
    string ip;
    int port;        // the peer's DHT port

    NodeInfo() {
        id = 0;
        port = 0;
    }

    // port 0 is never a real listening port, so it means "no node here"
    bool empty() const { return port == 0; }

    bool sameAs(const NodeInfo& other) const { return ip == other.ip && port == other.port; }

    // For humans: "4821@127.0.0.1:7000"
    string toString() const { return to_string(id) + "@" + ip + ":" + to_string(port); }

    // For messages: "4821 127.0.0.1 7000"
    string wire() const { return to_string(id) + " " + ip + " " + to_string(port); }
};

NodeInfo makeNode(unsigned int id, const string& ip, int port);

// Read "<id> <ip> <port>" starting at words[start]. Returns false if the words
// are missing or malformed, so a bad message can never corrupt the routing table.
bool parseNode(const vector<string>& words, int start, NodeInfo& out);
