// PeerServer.cpp - sends file pieces to other clients
#include "PeerServer.h"
#include "Protocol.h"
#include "Utils.h"

#include <fstream>

using namespace std;

PeerServer::PeerServer(SharedFiles& s) : shared(s) {
}

PeerServer::~PeerServer() {
    stop();
}

bool PeerServer::start(int port) {
    return startServer("0.0.0.0", port);   // 0.0.0.0 = accept on every network interface
}

void PeerServer::stop() {
    stopServer();
}

// Serve requests on one connection until the other peer hangs up
void PeerServer::handleConnection(Socket& sock) {
    sock.setTimeout(30);   // drop peers that connect and then go silent
    string request, data;
    while (sock.recvMessage(request)) {
        string status = readPiece(request, data);
        if (!sock.sendMessage(status)) return;
        if (status == "OK" && !sock.sendMessage(data)) return;
    }
}

// Read one piece from disk.
// The file is opened fresh for every request: if it was deleted we answer
// ERROR, and if it was modified the downloader's SHA1 check rejects the piece.
// In both cases the downloader fetches that piece from another seeder.
string PeerServer::readPiece(const string& request, string& data) {
    vector<string> words = splitWords(request);
    long long index;
    if (words.size() != 4 || words[0] != "GET_PIECE" || !parseInt(words[3], index) || index < 0) {
        return "ERROR: Bad request";
    }

    string path;
    if (!shared.find(words[1], words[2], path)) return "ERROR: File not shared";

    ifstream file(path, ios::binary | ios::ate);   // ios::ate = start at end -> tellg() = size
    if (!file) return "ERROR: Cannot open file";
    long long fileSize = file.tellg();
    if (index >= getPieceCount(fileSize)) return "ERROR: Piece index out of range";

    // Seek straight to the piece: offset = index * 512 KB
    long long len = getPieceSize(fileSize, index);
    data.resize(len);
    file.seekg(index * PIECE_SIZE);
    if (!file.read(&data[0], len)) return "ERROR: Read failed";
    return "OK";
}
