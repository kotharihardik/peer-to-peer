// Download.cpp - parallel multi-peer download of one file (see Download.h)
#include "Download.h"
#include "Protocol.h"
#include "Sha1.h"
#include "Socket.h"

#include <climits>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>

using namespace std;

const int MAX_WORKERS = 4;           // parallel piece downloads per file
const int MAX_PIECE_ROUNDS = 3;      // rounds over all seeders before giving up on a piece
const int MAX_SEEDER_FAILURES = 3;   // failures in a row -> treat seeder as offline

// ============================ FileHandle ============================

FileHandle::FileHandle() {
    fd = -1;
}

FileHandle::~FileHandle() {
    closeFile();
}

bool FileHandle::openForWrite(const string& path) {
    closeFile();
    fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    return fd >= 0;
}

int FileHandle::getFd() {
    return fd;
}

void FileHandle::closeFile() {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

// ============================ Download ============================

Download::Download(const string& sc, const FileMetadata& m, const vector<Seeder>& s, const string& path,
                   UploadManager& u)
    : uploads(u) {
    scope = sc;
    meta = m;
    seeders = s;
    destPath = path;
    cancelled = false;
    donePieces = 0;
    status = DOWNLOADING;

    long long pieceCount = meta.pieceHashes.size();
    for (long long i = 0; i < pieceCount; i++) pendingPieces.push_back(i);   // queue: 0,1,2,...
    pieceAttempts.assign(pieceCount, 0);
    seederFailures.assign(seeders.size(), 0);
    piecesFromSeeder.assign(seeders.size(), 0);
}

Download::~Download() {
    cancel();
    if (coordinator.joinable()) coordinator.join();
}

// Create the output file at its FULL size right away (ftruncate), so every
// worker can write its piece at the correct offset in any order.
bool Download::createOutputFile(string& errorMsg) {
    if (!outFile.openForWrite(destPath) || ftruncate(outFile.getFd(), meta.size) != 0) {
        errorMsg = "Cannot create destination file " + destPath;
        return false;
    }
    return true;
}

void Download::start() {
    coordinator = thread(&Download::run, this);
}

void Download::cancel() {
    cancelled = true;
}

bool Download::isActive() {
    lock_guard<mutex> lock(mtx);
    return status == DOWNLOADING;
}

string Download::getScope() {
    return scope;
}

string Download::getFileName() {
    return meta.name;
}

// Listing format:  [C] [scope] filename
string Download::getStatusLine() {
    lock_guard<mutex> lock(mtx);
    string name = "[" + scope + "] " + meta.name;
    if (status == COMPLETED) return "[C] " + name;
    if (status == FAILED) return "[F] " + name + "  (" + failReason + ")";

    long long total = meta.pieceHashes.size();
    long long percent = 100;
    if (total > 0) percent = donePieces * 100 / total;
    return "[D] " + name + "  " + to_string(percent) + "% (" + to_string(donePieces) + "/" +
           to_string(total) + " pieces)";
}

// ---------------- coordinator thread ----------------
// Starts the workers, waits for them, verifies the file, starts seeding.
void Download::run() {
    // 1. start the worker threads (never more workers than pieces)
    int workerCount = MAX_WORKERS;
    if ((long long)meta.pieceHashes.size() < workerCount) workerCount = meta.pieceHashes.size();

    vector<thread> workers;
    for (int i = 0; i < workerCount; i++) workers.push_back(thread(&Download::workerLoop, this));
    for (int i = 0; i < workerCount; i++) workers[i].join();   // wait for all

    // 2. all workers finished - was the download cancelled or did it fail?
    if (cancelled) setFinalStatus(FAILED, "cancelled");   // no effect if already FAILED
    outFile.closeFile();

    // 3. final check: SHA1 of the WHOLE file.
    // End-to-end check on top of the per-piece checks (catches e.g. write errors)
    if (isActive() && FileMetadata::hashWholeFile(destPath) != meta.sha1) {
        setFinalStatus(FAILED, "whole-file SHA1 mismatch");
    }

    if (!isActive()) {
        unlink(destPath.c_str());      // never leave a half/corrupt file behind
        logLine(getStatusLine());      // prints "[F] [scope] file (reason)"
        return;
    }

    setFinalStatus(COMPLETED, "");
    string sources;
    for (int i = 0; i < (int)seeders.size(); i++) {
        sources += " " + seeders[i].peerId + "=" + to_string(piecesFromSeeder[i]);
    }
    logLine("[C] [" + scope + "] " + meta.name + "  saved to " + destPath + "  (pieces from:" + sources + ")");

    // 4. auto-seed: we have the whole verified file, so publish it and serve it.
    // We reuse the hashes we already know, so no need to hash the file again.
    char fullPath[PATH_MAX];
    string path = destPath;
    if (realpath(destPath.c_str(), fullPath) != NULL) path = fullPath;
    string reply = uploads.share(scope, meta, path);
    logLine("[INFO] Now seeding " + meta.name + ": " + reply);
}

// ---------------- worker thread ----------------
// Take a piece -> try seeders one by one -> verify -> write. Repeat.
void Download::workerLoop() {
    int seederCount = seeders.size();
    long long index;

    while (!cancelled && takeNextPiece(index)) {
        bool ok = false;

        // Try each seeder once, starting at seeder (index % seederCount)
        for (int k = 0; k < seederCount && !ok && !cancelled; k++) {
            int s = (index + k) % seederCount;
            if (isSeederDead(s)) continue;   // failed too often -> skip it

            string data;
            if (!fetchPiece(seeders[s], index, data)) {
                logLine("[WARN] Piece " + to_string(index) + ": no answer from " + seeders[s].peerId);
            }
            else if (Sha1::hashOf(data.data(), data.size()) != meta.pieceHashes[index]) {
                // INTEGRITY CHECK: hash doesn't match -> corrupted -> discard it
                logLine("[WARN] Piece " + to_string(index) + " from " + seeders[s].peerId +
                        " failed SHA1 check, discarding");
            }
            else if (pwrite(outFile.getFd(), data.data(), data.size(), index * PIECE_SIZE) == (long long)data.size()) {
                // pwrite = write at a given offset. Safe from many threads at
                // once because it doesn't move a shared file position.
                ok = true;
                pieceDone(s);
            }
            recordSeederResult(s, ok);
        }

        if (!ok && !cancelled) pieceFailed(index);   // put it back in the queue (or give up)
    }
}

// Download one piece over a new short connection: request -> "OK" -> data.
// One short-lived connection per piece: a failing peer affects only that
// piece and there is no connection state to manage.
bool Download::fetchPiece(const Seeder& seeder, long long index, string& data) {
    Socket sock;
    if (!sock.connectTo(seeder.address.ip, seeder.address.port, 3)) return false;
    sock.setTimeout(10);   // a peer that dies mid-transfer can't block us forever

    string request = "GET_PIECE " + scope + " " + meta.name + " " + to_string(index);
    string status;
    if (!sock.sendMessage(request)) return false;
    if (!sock.recvMessage(status) || status != "OK") return false;
    if (!sock.recvMessage(data)) return false;

    // The size must be exactly right (the last piece may be smaller)
    return (long long)data.size() == getPieceSize(meta.size, index);
}

// ---------------- shared state helpers (each one locks mtx) ----------------

bool Download::takeNextPiece(long long& index) {
    lock_guard<mutex> lock(mtx);
    if (pendingPieces.empty() || status != DOWNLOADING) return false;
    index = pendingPieces.front();
    pendingPieces.pop_front();
    return true;
}

void Download::pieceDone(int seederIndex) {
    lock_guard<mutex> lock(mtx);
    donePieces++;
    piecesFromSeeder[seederIndex]++;
}

// No seeder could give this piece in this round -> retry later, or give up
void Download::pieceFailed(long long index) {
    bool giveUp;
    {
        lock_guard<mutex> lock(mtx);
        pieceAttempts[index]++;
        giveUp = pieceAttempts[index] >= MAX_PIECE_ROUNDS;
        if (!giveUp) pendingPieces.push_back(index);   // back of the queue
    }
    if (giveUp) setFinalStatus(FAILED, "piece " + to_string(index) + " unavailable from all seeders");
}

bool Download::isSeederDead(int seederIndex) {
    lock_guard<mutex> lock(mtx);
    return seederFailures[seederIndex] >= MAX_SEEDER_FAILURES;
}

// Count failures IN A ROW: one success resets the counter to 0
void Download::recordSeederResult(int seederIndex, bool ok) {
    lock_guard<mutex> lock(mtx);
    if (ok) seederFailures[seederIndex] = 0;
    else seederFailures[seederIndex]++;
}

// Only the FIRST final status counts. FAILED also sets "cancelled", which
// makes every other worker stop.
void Download::setFinalStatus(DownloadStatus newStatus, const string& reason) {
    lock_guard<mutex> lock(mtx);
    if (status != DOWNLOADING) return;
    status = newStatus;
    failReason = reason;
    if (newStatus == FAILED) cancelled = true;
}
