// FileMetadata.cpp - split a file into pieces and hash them (streaming)
#include "FileMetadata.h"
#include "Protocol.h"
#include "Sha1.h"

#include <fstream>
#include <sys/stat.h>

using namespace std;

string getFileName(const string& path) {
    size_t slash = path.find_last_of('/');
    if (slash == string::npos) return path;
    return path.substr(slash + 1);
}

bool FileMetadata::loadFromFile(const string& path, string& errorMsg) {
    // stat() gives the size and tells us if it is a normal file (not a folder)
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        errorMsg = "File not found or not a regular file";
        return false;
    }
    ifstream file(path, ios::binary);   // closed automatically (RAII)
    if (!file) {
        errorMsg = "Cannot open file";
        return false;
    }

    name = getFileName(path);
    size = st.st_size;
    pieceHashes.clear();

    // One pass over the file: each piece updates BOTH its own hash and the
    // whole-file hash. Only one piece is in memory at a time.
    Sha1 wholeFile;
    vector<char> buffer(PIECE_SIZE);
    long long pieceCount = getPieceCount(size);

    for (long long i = 0; i < pieceCount; i++) {
        long long len = getPieceSize(size, i);
        if (!file.read(buffer.data(), len)) {
            errorMsg = "Read error at piece " + to_string(i);
            return false;
        }
        pieceHashes.push_back(Sha1::hashOf(buffer.data(), len));
        wholeFile.update(buffer.data(), len);
    }
    sha1 = wholeFile.hexDigest();
    return true;
}

string FileMetadata::hashWholeFile(const string& path) {
    ifstream file(path, ios::binary);
    if (!file) return "";

    Sha1 sha;
    vector<char> buffer(PIECE_SIZE);
    while (true) {
        file.read(buffer.data(), buffer.size());
        long long got = file.gcount();   // last read may be shorter
        if (got <= 0) break;
        sha.update(buffer.data(), got);
    }
    return sha.hexDigest();
}
