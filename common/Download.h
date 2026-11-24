#pragma once
// =====================================================================
// Download - downloads ONE file from several peers at the same time
// =====================================================================
// ALGORITHM:
//
//  1. All piece numbers go into a queue: [0, 1, 2, ... N-1].
//  2. Up to 4 WORKER THREADS each take the next piece from the queue.
//  3. Piece i is first asked from seeder (i % numberOfSeeders). This
//     "round-robin" spreads the load, so different workers download from
//     DIFFERENT peers at the same time -> faster download.
//  4. Every piece is checked with SHA1 the moment it arrives.
//     Bad hash or no answer -> try the same piece from the next seeder.
//  5. A good piece is written directly at its final position in the output
//     file with pwrite(offset = index * 512 KB). No temp files.
//  6. When all pieces are done: check the whole-file SHA1, then automatically
//     start seeding the file (the downloader becomes a seeder).
//
// FAILURE HANDLING:
//  - Peer failure: connect timeout 3 s, read timeout 10 s; after 3 failures in
//    a row a seeder is skipped.
//  - A piece that fails on every seeder for 3 rounds fails the download; the
//    partial file is deleted and the status becomes [F].
//  - Memory: one 512 KB buffer per worker (pieces go straight to disk).
//  - Piece selection is round-robin because every seeder holds the complete
//    file (all pieces are equally available).
//
// This class is shared by both modules: it does not know whether the seeder
// list came from a central tracker or from the DHT.

#include "FileMetadata.h"
#include "MetadataIndex.h"
#include "UploadManager.h"
#include "Utils.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace std;

enum DownloadStatus { DOWNLOADING, COMPLETED, FAILED };

// Owns the output file descriptor; closes it automatically (RAII)
class FileHandle {
public:
    FileHandle();
    ~FileHandle();
    bool openForWrite(const string& path);
    int getFd();
    void closeFile();

private:
    int fd;
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
};

class Download {
public:
    Download(const string& scope, const FileMetadata& meta, const vector<Seeder>& seeders,
             const string& destPath, UploadManager& uploads);
    ~Download();   // cancels and waits for all threads

    bool createOutputFile(string& errorMsg);
    void start();                        // runs in the background
    void cancel();

    bool isActive();
    string getScope();
    string getFileName();
    string getStatusLine();              // one line for the "downloads" listing

private:
    void run();                          // coordinator thread
    void workerLoop();                   // body of each worker thread
    bool fetchPiece(const Seeder& seeder, long long index, string& data);

    // Shared queue / progress helpers (all lock the mutex)
    bool takeNextPiece(long long& index);
    void pieceDone(int seederIndex);
    void pieceFailed(long long index);
    bool isSeederDead(int seederIndex);
    void recordSeederResult(int seederIndex, bool ok);
    void setFinalStatus(DownloadStatus newStatus, const string& reason);

    // Set once in the constructor, then only read -> no lock needed
    string scope;
    FileMetadata meta;
    vector<Seeder> seeders;
    string destPath;
    UploadManager& uploads;

    FileHandle outFile;
    thread coordinator;
    atomic<bool> cancelled;              // atomic: read by all workers without a lock

    // Shared between worker threads -> protected by mtx
    mutex mtx;
    deque<long long> pendingPieces;      // pieces still to download
    vector<int> pieceAttempts;           // failed rounds per piece
    vector<int> seederFailures;          // failures in a row per seeder
    vector<long long> piecesFromSeeder;  // statistics: pieces received per seeder
    long long donePieces;
    DownloadStatus status;
    string failReason;
};
