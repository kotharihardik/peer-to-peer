// main.cpp (client) - usage:  ./client <IP>:<PORT> tracker_info.txt
// <IP>:<PORT> is where THIS client listens for piece requests from other peers.
#include "Client.h"
#include "Socket.h"
#include "Utils.h"

#include <iostream>

using namespace std;

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: ./client <IP>:<PORT> tracker_info.txt\n";
        return 1;
    }

    Address self;
    if (!parseAddress(argv[1], self)) {
        cerr << "Invalid address, expected <IP>:<PORT>\n";
        return 1;
    }

    vector<Address> trackers = readTrackerInfo(argv[2]);
    if (trackers.empty()) {
        cerr << "Cannot read trackers from " << argv[2] << "\n";
        return 1;
    }

    ignoreSigpipe();   // a peer disconnecting mid-transfer must not kill us

    Client client(self, trackers);
    if (!client.start()) return 1;
    client.run();

    // When "client" goes out of scope, its destructor stops downloads, the peer
    // server and the tracker connection - in the right order, automatically.
    return 0;
}
