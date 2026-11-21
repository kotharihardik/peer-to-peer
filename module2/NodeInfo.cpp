// NodeInfo.cpp
#include "NodeInfo.h"
#include "Hashing.h"

using namespace std;

NodeInfo makeNode(unsigned int id, const string& ip, int port) {
    NodeInfo n;
    n.id = id;
    n.ip = ip;
    n.port = port;
    return n;
}

bool parseNode(const vector<string>& words, int start, NodeInfo& out) {
    if ((int)words.size() < start + 3) return false;

    long long id = 0, port = 0;
    if (!parseInt(words[start], id) || !parseInt(words[start + 2], port)) return false;
    if (id < 0 || id >= (long long)ID_SPACE || port <= 0 || port > 65535) return false;

    out = makeNode((unsigned int)id, words[start + 1], (int)port);
    return true;
}
