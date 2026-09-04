#pragma once
//
// Thin socket helpers shared by the client and the server.
//
// The three bugs that sink this MP live in this file, so write it carefully and
// unit test it before touching anything else:
//
//   1. Partial reads/writes. TCP is a byte stream. A single read() may return
//      fewer bytes than you asked for, and a single write() may accept fewer
//      bytes than you handed it. ReadFull/WriteFull must loop.
//   2. EINTR. A blocking syscall interrupted by a signal returns -1/EINTR and
//      must be retried, not treated as a failure.
//   3. Unbounded blocking on a dead peer. A failed VM that drops packets (as
//      opposed to refusing the connection) will hang connect() or read() for
//      the OS default -- minutes. Every call here takes a deadline so a single
//      dead machine cannot stall the whole query.
//
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace mp1 {

using Clock    = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

// TODO: resolve `host` (getaddrinfo, AF_UNSPEC so both IPv4 and IPv6 work) and
// connect, giving up at `deadline`. Use a non-blocking socket + poll(POLLOUT)
// so the timeout is actually enforced; then check SO_ERROR to distinguish
// "connected" from "connection refused". Return -1 on failure, else the fd.
//
// Distinguish these for the caller, because the demo will exercise both:
//   ECONNREFUSED -> the VM is up but mp1d is not running
//   ETIMEDOUT    -> the VM itself is gone
int ConnectWithDeadline(const std::string& host, uint16_t port,
                        TimePoint deadline, std::string* err);

// TODO: bind + listen on `port` on all interfaces. Set SO_REUSEADDR so a
// restarted daemon does not fail on a lingering TIME_WAIT socket -- you will
// restart mp1d constantly while developing.
int Listen(uint16_t port, std::string* err);

// TODO: read exactly `len` bytes into `buf`, or fail at `deadline`.
// Loop on short reads, retry EINTR, and treat a clean EOF before `len` bytes as
// a truncation error (the peer died mid-message).
bool ReadFull(int fd, uint8_t* buf, size_t len, TimePoint deadline);

// TODO: write exactly `len` bytes. Same loop/EINTR rules.
// Also: ignore SIGPIPE process-wide (or send with MSG_NOSIGNAL) or writing to a
// peer that just died will kill your process instead of returning EPIPE.
bool WriteFull(int fd, const uint8_t* buf, size_t len, TimePoint deadline);

// TODO: read up to `len` bytes, returning however many arrived (>0), 0 on clean
// EOF, or -1 on error/timeout. This is the streaming read the client uses to
// drain DATA frames without knowing the total size in advance.
ssize_t ReadSome(int fd, uint8_t* buf, size_t len, TimePoint deadline);

}  // namespace mp1
