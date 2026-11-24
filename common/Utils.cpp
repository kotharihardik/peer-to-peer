// Utils.cpp - string parsing helpers + thread-safe logging
#include "Utils.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>

using namespace std;

vector<string> splitWords(const string& text) {
    vector<string> words;
    stringstream ss(text);
    string word;
    while (ss >> word) words.push_back(word);
    return words;
}

vector<string> splitLines(const string& text) {
    vector<string> lines;
    stringstream ss(text);
    string line;
    while (getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

bool parseInt(const string& text, long long& result) {
    if (text.empty()) return false;
    char* endPtr = NULL;
    errno = 0;
    long long value = strtoll(text.c_str(), &endPtr, 10);
    if (errno != 0 || *endPtr != '\0') return false;   // overflow or junk like "12abc"
    result = value;
    return true;
}

// Names travel inside space-separated text commands, so a space or '|'
// inside a name would break the protocol. We simply reject such names.
bool isValidName(const string& name) {
    if (name.empty() || name.size() > 255) return false;
    for (char c : name) {
        if (isspace((unsigned char)c) || c == '|') return false;
    }
    return true;
}

bool isSha1Hex(const string& text) {
    if (text.size() != 40) return false;
    for (char c : text) {
        bool digit = (c >= '0' && c <= '9');
        bool hexLetter = (c >= 'a' && c <= 'f');
        if (!digit && !hexLetter) return false;
    }
    return true;
}

bool parseAddress(const string& text, Address& result) {
    size_t colon = text.rfind(':');
    if (colon == string::npos) return false;
    long long port;
    if (!parseInt(text.substr(colon + 1), port) || port <= 0 || port > 65535) return false;
    result.ip = text.substr(0, colon);
    result.port = (int)port;
    return !result.ip.empty();
}

vector<Address> readTrackerInfo(const string& path) {
    vector<Address> trackers;
    ifstream file(path);
    Address a;
    while (file >> a.ip >> a.port) trackers.push_back(a);
    return trackers;
}

// All printing goes through here. Without the mutex, two threads printing at
// the same time produce mixed-up lines (ThreadSanitizer even reports it as a
// data race on cout).
void logLine(const string& msg, bool newline) {
    static mutex printMutex;   // "static" = one shared mutex, created once
    lock_guard<mutex> lock(printMutex);
    cout << msg;
    if (newline) cout << "\n";
    cout << flush;
}
