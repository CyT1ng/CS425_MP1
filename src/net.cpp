#include "mp1/net.hpp"

#include <unistd.h>

namespace mp1 {

// --- Conn lifetime --------------------------------------------------------
// Boilerplate RAII: the destructor closes, and moving transfers ownership by
// leaving the source invalid. Two Conns must never hold the same fd, which is
// why the copy operations are deleted in the header.

Conn::~Conn() {
    if (fd_ >= 0) ::close(fd_);
}

Conn::Conn(Conn&& other) noexcept : fd_(other.fd_), buf_(std::move(other.buf_)) {
    other.fd_ = -1;
}

Conn& Conn::operator=(Conn&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) ::close(fd_);
        fd_       = other.fd_;
        buf_      = std::move(other.buf_);
        other.fd_ = -1;
    }
    return *this;
}

// --- Conn I/O -------------------------------------------------------------

bool Conn::ReadLine(std::string* line, std::string* err) {
    (void)line; (void)err;
    // TODO: look for '\n' in buf_; if absent, read() more and append. When you
    // find one, hand back everything before it and ERASE it plus the newline
    // from buf_ -- the bytes after it belong to the next message.
    return false;
}

bool Conn::ReadExactly(size_t n, std::string* out, std::string* err) {
    (void)n; (void)out; (void)err;
    // TODO: take what buf_ already has, then loop read() until you have n.
    // read() returning 0 before then is the peer dying mid-message: fail.
    return false;
}

bool Conn::WriteAll(const std::string& data, std::string* err) {
    (void)data; (void)err;
    // TODO: loop until every byte is written; write() may accept only some.
    return false;
}

bool Conn::SetReadTimeout(std::chrono::milliseconds timeout, std::string* err) {
    (void)timeout; (void)err;
    // TODO: setsockopt(SO_RCVTIMEO) with a struct timeval.
    return false;
}

// --- listening / connecting ----------------------------------------------

int Listen(uint16_t port, std::string* err) {
    (void)port; (void)err;
    // TODO: socket, SO_REUSEADDR, bind, listen.
    return -1;
}

Conn Accept(int listen_fd, std::string* err) {
    (void)listen_fd; (void)err;
    // TODO: accept, wrap the fd in a Conn.
    return Conn();
}

Conn Connect(const std::string& host, uint16_t port,
             std::chrono::milliseconds timeout, std::string* err) {
    (void)host; (void)port; (void)timeout; (void)err;
    // TODO: getaddrinfo (AF_UNSPEC), non-blocking connect, poll(POLLOUT),
    // check SO_ERROR, then set the socket back to blocking.
    return Conn();
}

void IgnoreSigpipe() {
    // TODO: signal(SIGPIPE, SIG_IGN)
}

}  // namespace mp1
