#pragma once
//
// Wire protocol between the querier (dgrep) and each log server (mp1d).
//
// All integers are big-endian ("network byte order"). Every message is
// length-prefixed so that a reader never has to guess where a message ends.
//
// ---------------------------------------------------------------------------
// REQUEST  (client -> server, exactly one per connection)
// ---------------------------------------------------------------------------
//   magic    uint32   0x4D503143 ("MP1C") -- catches port collisions early
//   version  uint16   kProtocolVersion
//   argc     uint16   number of grep arguments that follow
//   argv[]   argc x { len uint32, bytes[len] }   raw argument strings, no NUL
//
// NOTE: the client sends ONLY the grep options/pattern. It never sends a file
// path. The server appends its own log filename. This guarantees (a) every
// machine greps its own machine.i.log and (b) a querier cannot read arbitrary
// files off a peer.
//
// ---------------------------------------------------------------------------
// RESPONSE  (server -> client, a stream of frames, terminated by TRAILER|ERROR)
// ---------------------------------------------------------------------------
//   DATA     0x01  len uint32, bytes[len]   raw grep stdout; may split a line
//   TRAILER  0x02  exit_code int32, line_count uint64      (ends the stream)
//   ERROR    0x03  len uint32, utf8 message                (ends the stream)
//
// Why a trailer instead of a header: the line count is not known until grep
// finishes. Sending it last lets the server stream results as they are produced
// instead of buffering the whole (possibly 24 MB) match set in memory first.
//
// The trailer also doubles as a completeness check: the client compares
// line_count against the number of lines it actually received. A mismatch means
// the peer died mid-stream, which is reported as PARTIAL rather than silently
// returning a short answer.
//
// grep exit codes are passed through verbatim and are NOT all failures:
//   0 = one or more lines matched
//   1 = no lines matched  <-- a valid, successful answer
//   2 = a real error (bad regex, unreadable file, ...)
//
#include <cstdint>
#include <string>
#include <vector>

namespace mp1 {

inline constexpr uint32_t kMagic           = 0x4D503143;
inline constexpr uint16_t kProtocolVersion = 1;

// Guardrails so a malformed or hostile frame cannot make the peer allocate
// unbounded memory. Reject anything larger and close the connection.
inline constexpr uint32_t kMaxArgLen   = 64u * 1024u;
inline constexpr uint16_t kMaxArgc     = 256;
inline constexpr uint32_t kMaxFrameLen = 8u * 1024u * 1024u;

enum class FrameType : uint8_t {
    kData    = 0x01,
    kTrailer = 0x02,
    kError   = 0x03,
};

struct Request {
    std::vector<std::string> argv;  // grep options + pattern; NO file path
};

// ---------------------------------------------------------------------------
// Serialization. These operate on byte buffers only -- they do no I/O, which is
// what makes them straightforward to unit test locally without any sockets.
// ---------------------------------------------------------------------------

// TODO: append the encoded request to `out` per the layout above.
void EncodeRequest(const Request& req, std::vector<uint8_t>& out);

// TODO: parse a request from `buf`. Return false on bad magic, unsupported
// version, or any length that exceeds the guardrails above. Must not read past
// the end of the buffer even when the length fields lie.
bool DecodeRequest(const std::vector<uint8_t>& buf, Request& out);

// TODO: frame writers. Each should build the header and hand the bytes to the
// caller's write path.
void EncodeDataFrame(const uint8_t* data, size_t len, std::vector<uint8_t>& out);
void EncodeTrailerFrame(int32_t exit_code, uint64_t line_count,
                        std::vector<uint8_t>& out);
void EncodeErrorFrame(const std::string& message, std::vector<uint8_t>& out);

}  // namespace mp1
