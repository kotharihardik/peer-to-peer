// Hashing.cpp - id space arithmetic. The two range checks are the only
// slightly tricky part of Chord, because the ring wraps around at 0.
#include "Hashing.h"
#include "Sha1.h"

#include <cstdlib>

using namespace std;

unsigned int hashToId(const string& text) {
    // Reuse the SHA1 class from common/. We only need the first 32 bits of the
    // digest, then fold that into the id space.
    string hex = Sha1::hashOf(text.data(), text.size());
    unsigned long value = strtoul(hex.substr(0, 8).c_str(), NULL, 16);
    return (unsigned int)(value % ID_SPACE);
}

unsigned int nodeIdOf(const string& ip, int port) {
    return hashToId(ip + ":" + to_string(port));
}

unsigned int fileKey(const string& scope, const string& fileName) {
    return hashToId(scope + "/" + fileName);
}

unsigned int addOnRing(unsigned int id, unsigned int offset) {
    return (unsigned int)(((unsigned long)id + offset) % ID_SPACE);
}

// (low, high]
bool inRangeExclIncl(unsigned int x, unsigned int low, unsigned int high) {
    if (low == high) return true;                 // one node owns the whole ring
    if (low < high) return x > low && x <= high;  // normal arc
    return x > low || x <= high;                  // arc that passes over 0
}

// (low, high)
bool inRangeExclExcl(unsigned int x, unsigned int low, unsigned int high) {
    if (low == high) return false;               // empty arc
    if (low < high) return x > low && x < high;
    return x > low || x < high;
}
