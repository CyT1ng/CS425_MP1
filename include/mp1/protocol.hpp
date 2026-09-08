#pragma once
//
// The conversation between log-query (the querier) and log-server (one
// machine's log daemon).
//
// Deliberately a TEXT protocol. Headers are ASCII lines; payloads are prefixed
// with their length so the reader always knows where they end. That costs a few
// bytes over a binary encoding and buys three things worth more on this project:
// no byte-order code, no bit shifting, and you can point `nc` at a daemon and
// read the whole exchange with your eyes.
//
// ---------------------------------------------------------------------------
// REQUEST   client -> server, exactly one per connection
// ---------------------------------------------------------------------------
//   ARGS <argc>\n
//   then argc times:   <len>\n<len bytes>\n
//
// The length is what delimits an argument, so an argument may contain newlines
// -- which is the whole reason arguments are length-prefixed instead of sent
// one per line. The newline AFTER the bytes is a terminator rather than a
// delimiter: it makes a request typeable straight into `nc` (the walkthrough in
// MP1_BUILD_GUIDE.md), and reading it back as an empty line proves the length
// was right instead of letting a wrong one shift every field after it. Data
// chunks below skip it -- they are bulk, machine-read, and already carry
// grep's own newlines.
//
// The client sends ONLY grep's options and pattern. It never sends a filename:
// the server appends its own. So every machine necessarily greps its own
// machine.<i>.log, and a querier cannot ask a peer for /etc/passwd -- there is
// no field to put a path in. That is structure doing the work, not a check you
// could forget.
//
// ---------------------------------------------------------------------------
// RESPONSE  server -> client: zero or more DATA lines, then exactly one END
// ---------------------------------------------------------------------------
//   D <len>\n<len bytes>              a chunk of grep's stdout; may split a line
//   E <exit_code> <line_count>\n      ends the stream
//
// The END line is the part that cannot be simplified away. Without it these
// three situations are identical on the wire -- all of them are "the connection
// closed and there was nothing in it":
//
//   grep exited 1, nothing matched        a correct answer
//   grep exited 2, bad regex              a real error
//   the machine was killed                a dead peer
//
// exit_code separates the first two. line_count, compared against the lines the
// client actually received, catches a peer that died halfway through sending.
// A stream that just stops, with no END line, is a peer that vanished.
//
// grep's exit codes pass through verbatim and are NOT all failures:
//   0 = one or more lines matched
//   1 = no lines matched   <-- a valid, successful answer
//   2 = a real error (bad regex, unreadable file, ...)
//
// ---------------------------------------------------------------------------
// Hardening: what is here, and what is deliberately not
// ---------------------------------------------------------------------------
//   Done: every length on the wire is a number the PEER chose, so each one is
//   range-checked against a cap in protocol.cpp BEFORE a single byte is
//   allocated for it. A malformed header ends one connection, never the daemon.
//
//   Deferred, on purpose:
//     A "MP1 <version>\n" greeting line, so pointing log-query at the wrong
//     port fails at hello rather than at the first frame. RecvFrame already
//     rejects the bytes clearly, so this buys a better message, not safety.
//     Carrying grep's stderr text back, so kGrepError says why and not just
//     that. It needs a second pipe and a poll() loop in grep_runner -- draining
//     one pipe while grep fills the other deadlocks.
//
#include <cstdint>
#include <string>
#include <vector>

#include "mp1/net.hpp"

namespace mp1 {

struct Request {
    std::vector<std::string> argv;  // grep options + pattern; NO file path
};

// A response frame, as read by the client.
struct Frame {
    bool        is_end     = false;  // false = data chunk, true = end of stream
    std::string data;                // when !is_end
    int         exit_code  = 0;      // when is_end
    uint64_t    line_count = 0;      // when is_end
};

// Failures split into two kinds, and the caller cares which: a socket that
// died (the machine is gone) and a peer that said something we cannot parse
// (that is not a log-server). Rather than a second error channel, every
// malformed-wire message from protocol.cpp begins with this prefix, and
// client.cpp keys MachineStatus::kProtocolError off it.
inline constexpr char kWireErrorPrefix[] = "protocol: ";

// --- client side ----------------------------------------------------------

// Writes the ARGS header, then each argument length-prefixed.
bool SendRequest(Conn& conn, const Request& req, std::string* err);

// Reads one frame: a header line, dispatched on its first character, then the
// payload. Anything else is a protocol error and is reported as one -- guessing
// would turn a wrong-port connection into nonsense output instead of a message.
bool RecvFrame(Conn& conn, Frame* out, std::string* err);

// --- server side ----------------------------------------------------------

// The mirror of SendRequest. Runs on data a stranger sent, so the header is
// validated rather than trusted -- see the caps in protocol.cpp.
bool RecvRequest(Conn& conn, Request* out, std::string* err);

// "D <len>\n" followed by the bytes. An empty chunk sends nothing.
bool SendData(Conn& conn, const std::string& chunk, std::string* err);

// "E <exit_code> <line_count>\n", sent even when grep failed -- the client
// needs the exit code to tell "no matches" from "broken".
bool SendEnd(Conn& conn, int exit_code, uint64_t line_count, std::string* err);

}  // namespace mp1
