#pragma once
// =====================================================================
// PeerServer - the "seeder" side of a client (inherits TcpServer)
// =====================================================================
// Every client is BOTH a downloader and a seeder. This class listens on the
// client's own port and answers other clients' requests:
//     "GET_PIECE <scope> <file> <index>"  ->  "OK" + the raw piece bytes
//
// File data goes directly from client to client; the tracker never handles it.

#include "SharedFiles.h"
#include "TcpServer.h"

#include <string>

using namespace std;

class PeerServer : public TcpServer {
public:
    PeerServer(SharedFiles& shared);
    ~PeerServer();

    bool start(int port);
    void stop();

protected:
    void handleConnection(Socket& sock) override;   // one thread per downloading peer

private:
    string readPiece(const string& request, string& data);   // returns "OK" or an error

    SharedFiles& shared;
};
