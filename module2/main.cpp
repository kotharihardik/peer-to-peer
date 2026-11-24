// main.cpp (module 2) - start one decentralized peer
//
//   First peer:  ./peer 127.0.0.1:7000
//   Others:      ./peer 127.0.0.1:7010 127.0.0.1:7000
//
// The second argument is any peer that is already in the ring. It is used only
// to find where this peer belongs; it is NOT a server and holds no special
// index. After joining, every peer is equal.
//
// Each peer uses two ports: the DHT port given here, and (DHT port + 100) for
// serving file pieces. So keep the DHT ports at least a little apart, e.g.
// 7000, 7010, 7020.
#include "Hashing.h"
#include "Peer.h"
#include "Socket.h"
#include "Utils.h"

#include <iostream>

using namespace std;

int main(int argc, char* argv[]) {
    // Helper for demos and scripts: print the node id an address would get,
    // without starting anything. Node ids are just hash(ip:port).
    if (argc == 3 && string(argv[1]) == "--id") {
        Address probe;
        if (!parseAddress(argv[2], probe)) {
            cerr << "Invalid address, expected <IP>:<PORT>\n";
            return 1;
        }
        cout << nodeIdOf(probe.ip, probe.port) << "\n";
        return 0;
    }

    if (argc != 2 && argc != 3) {
        cerr << "Usage: ./peer <IP>:<DHT_PORT> [<BOOTSTRAP_IP>:<BOOTSTRAP_PORT>]\n";
        cerr << "       ./peer --id <IP>:<PORT>     (print the node id for an address)\n";
        return 1;
    }

    Address dhtAddr;
    if (!parseAddress(argv[1], dhtAddr)) {
        cerr << "Invalid address, expected <IP>:<PORT>\n";
        return 1;
    }

    Address bootstrap;   // port 0 = none given, so this peer creates the ring
    if (argc == 3 && !parseAddress(argv[2], bootstrap)) {
        cerr << "Invalid bootstrap address, expected <IP>:<PORT>\n";
        return 1;
    }

    int transferPort = dhtAddr.port + TRANSFER_PORT_OFFSET;
    if (transferPort > 65535) {
        cerr << "DHT port is too high: the transfer port would be out of range\n";
        return 1;
    }
    Address transferAddr(dhtAddr.ip, transferPort);

    ignoreSigpipe();   // a peer disconnecting mid-transfer must not kill us

    Peer peer(dhtAddr, transferAddr);
    if (!peer.start(bootstrap)) return 1;
    peer.run();

    // The Peer destructor stops downloads, the piece server and the DHT node,
    // in that order, and joins every thread.
    return 0;
}
