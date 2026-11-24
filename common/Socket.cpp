// Socket.cpp - all low-level socket system calls of the project live here
#include "Socket.h"
#include "Protocol.h"

#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace std;

Socket::Socket() {
    fd = -1;
}

// RAII: socket is always closed when the object dies
Socket::~Socket() {
    closeSocket();
}

// Fill a sockaddr_in from "ip" and "port". Returns false for a bad IP/port.
static bool makeAddress(const string& ip, int port, sockaddr_in& addr) {
    addr = sockaddr_in();
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);                  // host -> network byte order
    if (port <= 0 || port > 65535) return false;
    return inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) == 1;
}

// Connect with a timeout.
// A blocking connect() to an unreachable host can hang for over a minute, so
// the socket is switched to non-blocking mode and poll() waits at most
// timeoutSec seconds.
bool Socket::connectTo(const string& ip, int port, int timeoutSec) {
    closeSocket();   // drop any old connection first

    sockaddr_in addr;
    if (!makeAddress(ip, port, addr)) return false;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;

    // 1. non-blocking mode -> connect() returns immediately
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int rc = connect(fd, (sockaddr*)&addr, sizeof(addr));
    if (rc < 0 && errno != EINPROGRESS) {
        closeSocket();
        return false;
    }

    // 2. connection still in progress -> wait until writable or timeout
    if (rc < 0) {
        pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        if (poll(&pfd, 1, timeoutSec * 1000) <= 0) {   // timeout or error
            closeSocket();
            return false;
        }
        int err = 0;
        socklen_t len = sizeof(err);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);   // e.g. "connection refused"
        if (err != 0) {
            closeSocket();
            return false;
        }
    }

    // 3. back to normal blocking mode for easy send/recv
    fcntl(fd, F_SETFL, flags);
    return true;
}

bool Socket::listenOn(const string& ip, int port) {
    closeSocket();

    sockaddr_in addr;
    if (!makeAddress(ip, port, addr)) return false;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;

    // SO_REUSEADDR: allows restarting the server immediately on the same port
    // (otherwise the port stays blocked for ~1 min in TIME_WAIT state).
    // SO_REUSEPORT is intentionally not used: it lets two processes bind the
    // same port silently.
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // "::bind" = the system call. The "::" is needed ONLY here: with
    // "using namespace std", plain bind() would pick std::bind (a totally
    // different function) and the socket would silently never be bound.
    if (::bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0 || listen(fd, 64) < 0) {
        closeSocket();
        return false;
    }
    return true;
}

void Socket::attach(int newFd) {
    closeSocket();
    fd = newFd;
}

bool Socket::isValid() {
    return fd >= 0;
}

int Socket::getFd() {
    return fd;
}

// Send/receive timeouts: a peer that stops responding makes send()/recv()
// fail after N seconds instead of blocking forever.
void Socket::setTimeout(int seconds) {
    timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

// Handles partial sends: send() may write fewer bytes than requested (e.g. when
// the kernel buffer is full), so loop until every byte is sent.
bool Socket::sendAll(const char* data, long long len) {
    long long sent = 0;
    while (sent < len) {
        long long n = send(fd, data + sent, len - sent, 0);
        if (n < 0 && errno == EINTR) continue;   // interrupted by a signal -> retry
        if (n <= 0) return false;               // connection broken
        sent += n;
    }
    return true;
}

// Same idea for receiving: recv() may return only part of the data.
bool Socket::recvAll(char* data, long long len) {
    long long got = 0;
    while (got < len) {
        long long n = recv(fd, data + got, len - got, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;   // 0 = peer closed, <0 = error or timeout
        got += n;
    }
    return true;
}

// Send one message: [4-byte length in network byte order][payload]
bool Socket::sendMessage(const string& msg) {
    if (msg.size() > MAX_MESSAGE_SIZE) return false;
    // htonl = "host to network long": both machines agree on byte order
    uint32_t header = htonl((uint32_t)msg.size());
    if (!sendAll((const char*)&header, 4)) return false;
    return sendAll(msg.data(), msg.size());
}

// Receive one message: read the 4-byte length, then exactly that many bytes
bool Socket::recvMessage(string& msg) {
    uint32_t header = 0;
    if (!recvAll((char*)&header, 4)) return false;
    uint32_t len = ntohl(header);

    // Reject oversized lengths (malformed or hostile input) before allocating
    if (len > MAX_MESSAGE_SIZE) return false;

    msg.assign(len, '\0');
    if (len == 0) return true;
    return recvAll(&msg[0], len);
}

// Used by the tracker sync link to notice that the other tracker died.
// The other side never sends us data on that link, so if the socket becomes
// "readable" it can only mean end-of-file (closed) or an error.
bool Socket::isClosedByPeer() {
    if (fd < 0) return true;
    pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    if (poll(&pfd, 1, 0) <= 0) return false;   // nothing happened -> still open
    char c;
    return recv(fd, &c, 1, MSG_PEEK | MSG_DONTWAIT) <= 0;
}

// shutdown() makes a recv() that is blocked in another thread return 0.
// This is how servers stop their connection threads cleanly.
void Socket::shutdownSocket() {
    if (fd >= 0) shutdown(fd, SHUT_RDWR);
}

void Socket::closeSocket() {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

// Writing to a socket whose peer has disconnected raises SIGPIPE, whose
// default action terminates the process. Ignoring it makes send() return an
// error instead, which is handled normally.
void ignoreSigpipe() {
    signal(SIGPIPE, SIG_IGN);
}
