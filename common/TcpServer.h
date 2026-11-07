#pragma once
// =====================================================================
// TcpServer - base class for every server in the project
// =====================================================================
// It accepts connections and runs each one in its own thread.
// What to DO with a connection is decided by the child class, by overriding
// the virtual function handleConnection().
//
// Derived classes:
//   Tracker             -> handles client commands
//   TrackerSynchronizer -> handles messages from the other tracker
//   PeerServer          -> sends file pieces to other clients
//
// Model: thread-per-connection (simple, sufficient for a small number of peers).

#include "Socket.h"

#include <atomic>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

using namespace std;

// One accepted client connection + the thread serving it
struct Connection {
    Socket sock;
    thread worker;
    atomic<bool> finished;
    Connection() { finished = false; }
};

class TcpServer {
public:
    TcpServer();
    virtual ~TcpServer();

    bool startServer(const string& ip, int port);
    void stopServer();

protected:
    // Child classes write their own logic here (runs on the connection's thread)
    virtual void handleConnection(Socket& sock) = 0;

private:
    void acceptLoop();
    void runConnection(Connection* conn);
    void cleanFinishedConnections();

    Socket listener;
    thread acceptThread;
    atomic<bool> running;

    // Connection objects are owned by this list (unique_ptr = auto delete).
    // The socket stays open until the thread is joined, so stopServer() can
    // safely call shutdown() on it.
    mutex connMutex;
    list<unique_ptr<Connection>> connections;
};
