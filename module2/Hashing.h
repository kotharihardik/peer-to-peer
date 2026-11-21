#pragma once
// =====================================================================
// Hashing - the consistent-hashing identifier space (the "ring")
// =====================================================================
// Both peers and files are hashed into the SAME circular id space
// 0 .. ID_SPACE-1, and the rule is:
//
//     the node responsible for a key is the first node CLOCKWISE from it
//
// So a node with id n owns every key in the range (predecessor, n].
//
// Why not simply hash(file) % numberOfPeers?
//   Because that divides by the NUMBER of peers: if one peer joins or leaves,
//   almost every key gets a different owner and the whole index must move.
//   With a ring, a joining node only takes keys from its single successor, so
//   on average just 1/N of the keys move. That property is the whole point of
//   consistent hashing.

#include <string>

using namespace std;

// 16-bit ids: 65536 slots. Small enough to print in demos, large enough that
// two peers practically never collide.
const int ID_BITS = 16;
const unsigned int ID_SPACE = 1u << ID_BITS;

// SHA1 of the text, folded down into the id space
unsigned int hashToId(const string& text);

// A peer's node id comes from its address, so a restarted peer keeps its id
unsigned int nodeIdOf(const string& ip, int port);

// A file's key. The scope is included so two different namespaces could hold
// the same file name without clashing.
unsigned int fileKey(const string& scope, const string& fileName);

// Walking forward on the circle: (id + offset) wraps around at ID_SPACE
unsigned int addOnRing(unsigned int id, unsigned int offset);

// Is x inside the arc (low, high] going clockwise?  low == high means
// "the whole ring", which is the case when a node is alone.
bool inRangeExclIncl(unsigned int x, unsigned int low, unsigned int high);

// Is x inside the arc (low, high) going clockwise?  Both ends excluded.
bool inRangeExclExcl(unsigned int x, unsigned int low, unsigned int high);
