#pragma once
// Utils - small helper functions used by both tracker and client

#include <string>
#include <vector>
using namespace std;

// An "ip:port" pair
struct Address {
    string ip;
    int port;

    Address() { port = 0; }
    Address(const string& i, int p) { ip = i; port = p; }
    string toString() const { return ip + ":" + to_string(port); }
};

// "join_group  g1" -> {"join_group", "g1"}
vector<string> splitWords(const string& text);

// Split text into lines
vector<string> splitLines(const string& text);

// String -> number WITHOUT throwing exceptions (stoi() throws on bad input,
// and one bad network message must never crash the program)
bool parseInt(const string& text, long long& result);

// IDs / file names: not empty, no spaces, no '|', max 255 chars
bool isValidName(const string& name);

// A SHA1 hash in hex = exactly 40 characters of 0-9 / a-f
bool isSha1Hex(const string& text);

// "127.0.0.1:6001" -> Address
bool parseAddress(const string& text, Address& result);

// Read "ip port" lines from tracker_info.txt
vector<Address> readTrackerInfo(const string& path);

// Thread-safe printing (one mutex for all threads, so lines never mix)
void logLine(const string& msg, bool newline = true);
