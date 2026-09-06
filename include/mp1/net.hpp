#pragma once
//
// Sockets, wrapped in one small object.
//
// TCP hands you a byte stream: a read() may return fewer bytes than you asked
// for, and a write() may accept fewer than you gave it. Rather than make every
// caller remember that, all the looping lives in Conn, and the rest of the
// program works in terms of "give me a line" and "give me exactly N bytes".
//
// Conn owns its file descriptor and closes it in the destructor, so no code
// path can leak a socket by returning early. It is movable but not copyable --
// two objects closing the same fd would be a bug.
//
#include <chrono>
#include <cstdint>
#include <string>

namespace mp1 {

class Conn {
public:
    Conn() = default;                 // invalid; valid() is false
    explicit Conn(int fd) : fd_(fd) {}
    ~Conn();

    Conn(Conn&& other) noexcept;
    Conn& operator=(Conn&& other) noexcept;
    Conn(const Conn&)            = delete;
    Conn& operator=(const Conn&) = delete;

    bool valid() const { return fd_ >= 0; }
    int  fd()    const { return fd_; }

    // TODO: read up to and including the next '\n', return the line WITHOUT it.
    // Needs a small internal buffer: a single read() can straddle a newline or
    // return several lines at once, so leftover bytes must survive to the next
    // call. That buffer is why this is a class and not three free functions.
    // Returns false on EOF before any '\n', or on error.
    bool ReadLine(std::string* line, std::string* err);

    // TODO: read exactly `n` bytes. Loop until you have them all. A clean EOF
    // partway through means the peer died mid-message -- that is a failure, and
    // it is exactly how a killed VM is detected. Drain the internal buffer from
    // ReadLine first.
    bool ReadExactly(size_t n, std::string* out, std::string* err);

    // TODO: write all of `data`, looping on short writes.
    bool WriteAll(const std::string& data, std::string* err);

    // TODO: SO_RCVTIMEO, so a peer that goes silent fails instead of hanging
    // forever. Note this does NOT apply to connect() -- see Connect below.
    bool SetReadTimeout(std::chrono::milliseconds timeout, std::string* err);

private:
    int         fd_ = -1;
    std::string buf_;   // bytes read but not yet consumed
};

// TODO: bind + listen on `port`, all interfaces. Set SO_REUSEADDR or a
// restarted daemon fails on a lingering TIME_WAIT socket -- and you will
// restart mp1d constantly. Returns the listening fd, or -1.
int Listen(uint16_t port, std::string* err);

// TODO: accept one connection. Returns an invalid Conn on failure.
Conn Accept(int listen_fd, std::string* err);

// TODO: resolve `host` and connect, giving up after `timeout`.
//
// This one cannot use SO_RCVTIMEO: Linux ignores socket timeouts for connect().
// Use a non-blocking socket + poll(POLLOUT), then check SO_ERROR to tell
// "connected" from "refused". Without this, one dead VM stalls the whole query
// for the OS default -- minutes.
//
// Both outcomes show up at the demo, so keep them distinguishable in `err`:
//   ECONNREFUSED -> the VM is up, mp1d is not running
//   ETIMEDOUT    -> the VM itself is gone
Conn Connect(const std::string& host, uint16_t port,
             std::chrono::milliseconds timeout, std::string* err);

// TODO: ignore SIGPIPE process-wide. Call once at the top of main in both
// binaries. Without it, writing to a peer that just died kills your process
// instead of returning an error -- which looks exactly like a crash bug.
void IgnoreSigpipe();

}  // namespace mp1
