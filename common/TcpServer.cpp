// TcpServer.cpp - accept loop + one thread per connection + clean shutdown
#include "TcpServer.h"

#include <poll.h>
#include <sys/socket.h>

using namespace std;

TcpServer::TcpServer() {
    running = false;
}

// Note: child classes call stopServer() in THEIR destructor. By the time this
// base destructor runs, the child part is already destroyed, so threads must
// not be calling the child's handleConnection() any more.
TcpServer::~TcpServer() {
    stopServer();
}

// Open the listening socket and start the accept thread
bool TcpServer::startServer(const string& ip, int port) {
    if (!listener.listenOn(ip, port)) return false;
    running = true;
    acceptThread = thread(&TcpServer::acceptLoop, this);
    return true;
}

// Runs on its own thread: wait for new clients and start a thread for each
void TcpServer::acceptLoop() {
    while (running) {
        // poll() with a 200 ms timeout instead of a blocking accept(), so the
        // loop regularly checks "running" and can stop when asked.
        pollfd pfd;
        pfd.fd = listener.getFd();
        pfd.events = POLLIN;
        pfd.revents = 0;
        if (poll(&pfd, 1, 200) <= 0) {
            cleanFinishedConnections();
            continue;
        }

        int fd = accept(listener.getFd(), NULL, NULL);
        if (fd < 0) continue;

        // Create the connection and start its thread
        Connection* conn = new Connection();
        conn->sock.attach(fd);
        conn->worker = thread(&TcpServer::runConnection, this, conn);

        lock_guard<mutex> lock(connMutex);
        connections.push_back(unique_ptr<Connection>(conn));   // list now owns it
    }
}

// Body of every connection thread
void TcpServer::runConnection(Connection* conn) {
    try {
        handleConnection(conn->sock);   // virtual call -> child class logic
    } catch (...) {
        // One bad connection must never crash the whole server
    }
    conn->sock.shutdownSocket();
    conn->finished = true;
}

// Join threads that already finished and free their memory.
// Without this the list would grow forever (a slow memory leak).
void TcpServer::cleanFinishedConnections() {
    lock_guard<mutex> lock(connMutex);
    auto it = connections.begin();
    while (it != connections.end()) {
        if ((*it)->finished) {
            (*it)->worker.join();
            it = connections.erase(it);   // unique_ptr deletes it, Socket closes fd
        } else {
            it++;
        }
    }
}

// Clean shutdown: stop accepting, wake every connection thread, join them all.
// shutdown() on each socket makes any blocked recv() return 0, so every
// handler exits its loop and can be joined. No thread is detached.
void TcpServer::stopServer() {
    if (!running) return;
    running = false;
    if (acceptThread.joinable()) acceptThread.join();
    listener.closeSocket();

    lock_guard<mutex> lock(connMutex);
    for (auto& conn : connections) conn->sock.shutdownSocket();
    for (auto& conn : connections) conn->worker.join();
    connections.clear();
}
