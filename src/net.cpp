#include "mp1/net.hpp"

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

namespace mp1 {
namespace {

// How much we are willing to take off the socket in one read(). Headers are a
// few dozen bytes, but payloads are whatever grep produced, so a generous
// buffer keeps the syscall count down on a frequent query.
constexpr size_t kReadChunk = 64 * 1024;

// Connections the kernel may queue while we are busy inside accept(). Ten
// machines querying at once never comes close; a large backlog costs nothing.
constexpr int kBacklog = 64;

// The longest header line we will accumulate before giving up. Every header in
// this protocol is a few dozen bytes; payloads never come through ReadLine at
// all. A peer that sends more than this without a newline is either not
// speaking our protocol or trying to make us buffer until we die -- and it is
// the peer, not us, that chose how much to send.
constexpr size_t kMaxLineLen = 64 * 1024;

// Helper for the Conn methods below. If `err` is non-null, writes a message
// that includes the current errno. Returns false, so callers can just `return
// Fail(...)`.
bool Fail(std::string* err, const char* what) {
    if (err) *err = std::string(what) + ": " + std::strerror(errno);
    return false;
}

// The same, for a function that hands back an fd instead of a bool. The message
// is built BEFORE the close, because close() is allowed to overwrite errno.
int CloseAndFail(int fd, std::string* err, const char* what) {
    Fail(err, what);
    ::close(fd);
    return -1;
}

// Turns O_NONBLOCK on or off. Connect needs it on to get a real timeout out of
// poll(), and off again afterwards so the rest of the program can do ordinary
// blocking reads and writes.
bool SetNonBlocking(int fd, bool on, std::string* err) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) return Fail(err, "fcntl(F_GETFL)");
    flags = on ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    if (::fcntl(fd, F_SETFL, flags) < 0) return Fail(err, "fcntl(F_SETFL)");
    return true;
}

// Waits for an in-flight connect() to finish. Returns 1 if the socket became
// writable, 0 if `timeout` ran out, -1 on error with errno set.
//
// The deadline is computed once, so a signal costs one retry rather than
// restarting the whole budget -- otherwise a machine could stall us for far
// longer than the caller asked for.
int WaitUntilWritable(int fd, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0) return 0;

        struct pollfd pfd = {fd, POLLOUT, 0};
        const int ready = ::poll(&pfd, 1, static_cast<int>(left.count()));
        if (ready >= 0) return ready;
        if (errno != EINTR) return -1;
    }
}

// Connects to ONE resolved address. Connect() below calls this for each address
// getaddrinfo returned, which is what makes a host with both an IPv6 and an
// IPv4 address work when only one of the two is actually reachable.
Conn ConnectOne(const addrinfo* ai, std::chrono::milliseconds timeout,
                std::string* err) {
    // Wrapped in a Conn immediately: every failure path below can then just
    // return, and the destructor closes the socket for us.
    Conn conn(::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol));
    if (!conn.valid()) {
        Fail(err, "socket");
        return Conn();
    }
    if (!SetNonBlocking(conn.fd(), true, err)) return Conn();

    if (::connect(conn.fd(), ai->ai_addr, ai->ai_addrlen) != 0) {
        // Anything other than "started, ask again later" is a real refusal.
        if (errno != EINPROGRESS) {
            Fail(err, "connect");
            return Conn();
        }
        const int ready = WaitUntilWritable(conn.fd(), timeout);
        if (ready < 0) {
            Fail(err, "poll");
            return Conn();
        }
        if (ready == 0) {
            // The VM itself is gone: nothing answered, not even a refusal.
            if (err) {
                *err = "connect timed out after " +
                       std::to_string(timeout.count()) + " ms";
            }
            return Conn();
        }
        // Writable only means the attempt FINISHED. SO_ERROR says how.
        int so_error = 0;
        socklen_t len = sizeof(so_error);
        if (::getsockopt(conn.fd(), SOL_SOCKET, SO_ERROR, &so_error, &len) < 0) {
            Fail(err, "getsockopt(SO_ERROR)");
            return Conn();
        }
        if (so_error != 0) {
            // Usually ECONNREFUSED: the VM is up, log-server is not running.
            if (err) *err = std::string("connect: ") + std::strerror(so_error);
            return Conn();
        }
    }

    if (!SetNonBlocking(conn.fd(), false, err)) return Conn();
    return conn;
}

}  // namespace

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

// Hands back one complete line, without its '\n'.
//
// The loop reads more only when the buffer does not already hold a newline,
// which is the whole point of buf_: a single read() can stop halfway through a
// header or deliver three of them at once, and the bytes past the newline
// belong to the NEXT message. Dropping them would silently lose a frame.
bool Conn::ReadLine(std::string* line, std::string* err) {
    for (;;) {
        const size_t newline = buf_.find('\n');
        if (newline != std::string::npos) {
            line->assign(buf_, 0, newline);
            buf_.erase(0, newline + 1);
            return true;
        }

        if (buf_.size() > kMaxLineLen) {
            if (err) {
                *err = "no end of line in " + std::to_string(buf_.size()) +
                       " bytes; the peer is not speaking this protocol";
            }
            return false;
        }

        char chunk[kReadChunk];
        const ssize_t n = ::read(fd_, chunk, sizeof(chunk));
        if (n < 0) {
            if (errno == EINTR) continue;  // a signal, not a failure
            return Fail(err, "read");
        }
        if (n == 0) {
            if (err) *err = "peer closed the connection mid-line";
            return false;
        }
        buf_.append(chunk, static_cast<size_t>(n));
    }
}

// Hands back exactly `n` bytes, used for payloads whose length a header just
// announced. A short answer is never acceptable here: EOF partway through means
// the peer died mid-message, which is precisely how a killed VM is detected.
bool Conn::ReadExactly(size_t n, std::string* out, std::string* err) {
    out->clear();
    out->reserve(n);

    // Whatever ReadLine already pulled off the socket comes first.
    const size_t from_buf = std::min(n, buf_.size());
    out->append(buf_, 0, from_buf);
    buf_.erase(0, from_buf);

    char chunk[kReadChunk];
    while (out->size() < n) {
        const size_t want = std::min(sizeof(chunk), n - out->size());
        const ssize_t got = ::read(fd_, chunk, want);
        if (got < 0) {
            if (errno == EINTR) continue;
            return Fail(err, "read");
        }
        if (got == 0) {
            if (err) {
                *err = "peer closed the connection after " +
                       std::to_string(out->size()) + " of " +
                       std::to_string(n) + " bytes";
            }
            return false;
        }
        out->append(chunk, static_cast<size_t>(got));
    }
    return true;
}

// Writes every byte. A single write() may accept only part of what it was
// given -- on a 24 MB result it almost always does -- so the caller would
// otherwise have to loop, everywhere.
bool Conn::WriteAll(const std::string& data, std::string* err) {
    size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::write(fd_, data.data() + sent, data.size() - sent);
        if (n < 0) {
            if (errno == EINTR) continue;  // signal interrupted the syscall
            return Fail(err, "write");
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

// A peer that CRASHES closes its socket and our next read returns 0. A peer
// that FREEZES does neither, and without this the read blocks forever.
bool Conn::SetReadTimeout(std::chrono::milliseconds timeout, std::string* err) {
    struct timeval tv;
    tv.tv_sec  = static_cast<time_t>(timeout.count() / 1000);
    tv.tv_usec = static_cast<suseconds_t>((timeout.count() % 1000) * 1000);
    if (::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        return Fail(err, "setsockopt(SO_RCVTIMEO)");
    }
    return true;
}

// --- listening / connecting ----------------------------------------------

int Listen(uint16_t port, std::string* err) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        Fail(err, "socket");
        return -1;
    }

    // Without SO_REUSEADDR, a restart within the TIME_WAIT window fails to bind
    // -- and you restart log-server constantly while developing.
    int on = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
        return CloseAndFail(fd, err, "setsockopt(SO_REUSEADDR)");
    }

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);  // all interfaces: peers are remote
    addr.sin_port        = htons(port);

    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        return CloseAndFail(fd, err, "bind");
    }
    if (::listen(fd, kBacklog) < 0) {
        return CloseAndFail(fd, err, "listen");
    }
    return fd;
}

Conn Accept(int listen_fd, std::string* err) {
    for (;;) {
        const int fd = ::accept(listen_fd, nullptr, nullptr);
        if (fd >= 0) return Conn(fd);
        if (errno == EINTR) continue;
        Fail(err, "accept");
        return Conn();
    }
}

// Resolves `host` and connects, giving up after `timeout`.
//
// A hostname can resolve to several addresses (IPv6 and IPv4, or a round-robin
// set), and only some of them may be reachable, so each is tried in turn --
// sharing one deadline, so N unreachable addresses still cost one timeout in
// total rather than N.
Conn Connect(const std::string& host, uint16_t port,
             std::chrono::milliseconds timeout, std::string* err) {
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;    // IPv4 or IPv6, whichever the host has
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* addresses = nullptr;
    const int rc = ::getaddrinfo(host.c_str(), std::to_string(port).c_str(),
                                 &hints, &addresses);
    if (rc != 0) {
        // A typo in machines.txt lands here, so name the host that failed.
        if (err) *err = "resolve " + host + ": " + ::gai_strerror(rc);
        return Conn();
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::string last_error = host + ": no usable address";

    Conn conn;
    for (const addrinfo* ai = addresses; ai != nullptr; ai = ai->ai_next) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0) {
            last_error = "connect timed out after " +
                         std::to_string(timeout.count()) + " ms";
            break;
        }
        conn = ConnectOne(ai, left, &last_error);
        if (conn.valid()) break;
    }
    ::freeaddrinfo(addresses);

    if (!conn.valid() && err) *err = last_error;
    return conn;
}

// Writing to a connection the peer already closed raises SIGPIPE, whose default
// action is to KILL the process -- not an error code you can check. At the demo
// a grader kills a VM; without this, one dead machine takes our daemon with it
// and a single failure turns into several.
void IgnoreSigpipe() {
    ::signal(SIGPIPE, SIG_IGN);
}

}  // namespace mp1
