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
//   then argc times:   <len>\n<len bytes>
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
// Deferred, on purpose -- add before the cluster demo
// ---------------------------------------------------------------------------
//   A "MP1 <version>\n" greeting line, so pointing log-query at the wrong port
//   fails loudly instead of parsing another service's bytes as grep output.
//   Length caps (kMaxArgs, kMaxLineLen): a length is a number the PEER chose.
//   Carrying grep's stderr text back, so an error says why and not just that.
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

// --- client side ----------------------------------------------------------

// TODO: write the ARGS header, then each argument length-prefixed.
bool SendRequest(Conn& conn, const Request& req, std::string* err);

// TODO: read one frame. Read a header line, dispatch on its first character,
// then read the payload. Anything else is a protocol error -- say so rather
// than guessing, or a wrong-port connection turns into nonsense output.
bool RecvFrame(Conn& conn, Frame* out, std::string* err);

// --- server side ----------------------------------------------------------

// TODO: the mirror of SendRequest. Reject a malformed header instead of
// trusting it; this runs on data a stranger sent you.
bool RecvRequest(Conn& conn, Request* out, std::string* err);

// TODO: "D <len>\n" followed by the bytes.
bool SendData(Conn& conn, const std::string& chunk, std::string* err);

// TODO: "E <exit_code> <line_count>\n". Send this even when grep failed --
// the client needs the exit code to tell "no matches" from "broken".
bool SendEnd(Conn& conn, int exit_code, uint64_t line_count, std::string* err);

}  // namespace mp1
