#pragma once
// =====================================================================
// Protocol.h - constants shared by tracker and client + protocol summary
// =====================================================================
//
// MESSAGE FRAMING (used on every connection in the project)
//   Every message = [4-byte length][payload bytes]
//
//   Length-prefix framing: TCP is a byte stream, so one recv() may return
//   part of a message or several messages together. The receiver reads the
//   4-byte length first, then exactly that many bytes. Works for both text
//   and binary (piece) data.
//
// THE THREE CONNECTION TYPES
//   1. Client  -> Tracker : text commands, e.g. "join_group g1"
//                           replies start with "SUCCESS:" or "ERROR:"
//   2. Client  -> Client  : "GET_PIECE <scope> <file> <index>"
//                           reply: "OK" + one message with the raw piece bytes
//                                  or a single "ERROR: ..." message
//   3. Tracker -> Tracker : "SNAPSHOT_REQUEST"  -> full state as sync ops
//                           "OPS\n<op lines>"   -> apply these changes

// Files are split into pieces of exactly 512 KB (the last piece can be smaller)
const long long PIECE_SIZE = 512 * 1024;

// Reject any message bigger than 64 MB (protects against garbage length headers)
const unsigned int MAX_MESSAGE_SIZE = 64 * 1024 * 1024;

// Each tracker listens for the other tracker on (its client port + 1000)
const int SYNC_PORT_OFFSET = 1000;

// Number of pieces for a file. Example: 1.2 MB -> 3 pieces (512K + 512K + 176K)
inline long long getPieceCount(long long fileSize) {
    return (fileSize + PIECE_SIZE - 1) / PIECE_SIZE;   // ceiling division
}

// Size of piece number "index" (only the last piece can be smaller)
inline long long getPieceSize(long long fileSize, long long index) {
    long long remaining = fileSize - index * PIECE_SIZE;
    if (remaining < PIECE_SIZE) return remaining;
    return PIECE_SIZE;
}
