// main.cpp (tracker) - usage:  ./tracker tracker_info.txt <tracker_no>
#include "Socket.h"
#include "Tracker.h"
#include "Utils.h"

#include <csignal>
#include <cstring>
#include <iostream>
#include <poll.h>
#include <unistd.h>

using namespace std;

// The only global variable: a signal handler takes no parameters and may only
// safely set a sig_atomic_t flag. The main loop checks it and shuts down.
volatile sig_atomic_t stopRequested = 0;

void onSignal(int) {
    stopRequested = 1;
}

// Wait until the user types "quit" or presses Ctrl-C (SIGINT) / gets SIGTERM
void waitForQuit() {
    bool stdinOpen = true;
    while (!stopRequested) {
        // poll() with a timeout, so we also check stopRequested twice a second
        pollfd pfd;
        pfd.fd = STDIN_FILENO;
        pfd.events = POLLIN;
        pfd.revents = 0;
        int watched = stdinOpen ? 1 : 0;   // stdin closed (background run) -> only wait
        if (poll(&pfd, watched, 500) <= 0) continue;

        string line;
        if (!getline(cin, line)) {
            stdinOpen = false;
            continue;
        }
        if (line == "quit") return;
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: ./tracker tracker_info.txt <tracker_no>\n";
        return 1;
    }

    vector<Address> trackers = readTrackerInfo(argv[1]);
    long long trackerNo = 0;
    if (trackers.empty() || !parseInt(argv[2], trackerNo) || trackerNo < 1 || trackerNo > (long long)trackers.size()) {
        cerr << "Invalid tracker info file or tracker number\n";
        return 1;
    }

    ignoreSigpipe();

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = onSignal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    Tracker tracker(trackers, (int)trackerNo - 1);
    if (!tracker.start()) return 1;

    waitForQuit();

    logLine("Shutting down tracker...");
    tracker.stop();   // joins every thread and closes every socket
    return 0;
}
