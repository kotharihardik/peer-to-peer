#pragma once
// =====================================================================
// FileMetadata - what the tracker knows about a shared file
// =====================================================================
// name + size + SHA1 of the whole file + SHA1 of every 512 KB piece.
// The piece hashes let a downloader check EACH piece the moment it arrives;
// the whole-file hash is a final end-to-end check.

#include <string>
#include <vector>

using namespace std;

class FileMetadata {
public:
    string name;                  // file name (no directory part)
    long long size = 0;
    string sha1;                  // hash of the complete file
    vector<string> pieceHashes;   // pieceHashes[i] = hash of piece i

    // Read the file ONCE and compute all piece hashes and the whole-file hash
    // at the same time. Memory used: one 512 KB buffer, even for a 1 GB file.
    bool loadFromFile(const string& path, string& errorMsg);

    // Whole-file SHA1 of a file on disk (used to verify a finished download)
    static string hashWholeFile(const string& path);
};

// "/home/a/movie.mp4" -> "movie.mp4"
string getFileName(const string& path);
