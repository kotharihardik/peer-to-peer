#pragma once
// =====================================================================
// Socket - a small class that owns one TCP socket
// =====================================================================
// RAII: the destructor closes the socket, so the fd is released on every
// exit path (normal return, early return, exception).

#include <string>
using namespace std;

class Socket {
public:
    Socket();
    ~Socket();

    // Client side: connect to ip:port, give up after timeoutSec seconds
    bool connectTo(const string& ip, int port, int timeoutSec);

    // Server side: bind to ip:port and start listening
    bool listenOn(const string& ip, int port);

    // Take ownership of an already-open fd (used after accept())
    void attach(int newFd);

    bool isValid();
    int getFd();

    // Max seconds a send/recv may block (0 = forever)
    void setTimeout(int seconds);

    // Send / receive EXACTLY len bytes (loops over partial send/recv)
    bool sendAll(const char* data, long long len);
    bool recvAll(char* data, long long len);

    // Send / receive one complete message using length-prefix framing
    bool sendMessage(const string& msg);
    bool recvMessage(string& msg);

    // Non-blocking check: has the other side closed the connection?
    bool isClosedByPeer();

    // Wake up a thread that is blocked in recv() on this socket
    void shutdownSocket();

    void closeSocket();

private:
    int fd;   // -1 means "no socket"

    // Copying is disabled: two objects owning the same fd would close it twice
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
};

// Ignore SIGPIPE for the whole process (see Socket.cpp for why)
void ignoreSigpipe();
