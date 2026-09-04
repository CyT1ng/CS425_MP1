#pragma once
//
// Wire protocol between the querier (dgrep) and each log server (mp1d).
//
// VERSION 1 -- deliberately minimal. Get the system talking end to end first;
// the deferred pieces are listed at the bottom and are each a small edit to
// this file plus protocol.cpp. Both ends are built from this header and there
// is no deployed version to stay compatible with, so adding fields later costs
// nothing but a rebuild.
//
// All integers are big-endian ("network byte order"). Every variable-length
// field is length-prefixed, so a reader never has to guess where it ends --
// TCP delivers a byte stream, not messages, and one read() can return half a
// message or two of them.
//
// ---------------------------------------------------------------------------
// REQUEST  (client -> server, exactly one per connection)
// ---------------------------------------------------------------------------
//   argc     uint32   number of grep arguments that follow
//   argv[]   argc x { len uint32, bytes[len] }   raw argument strings, no NUL
//
// NOTE: the client sends ONLY the grep options/pattern. It never sends a file
// path. The server appends its own log filename. This guarantees (a) every
// machine greps its own machine.i.log and (b) a querier cannot read arbitrary
// files off a peer. Keep this true even in v1 -- it is structural, not a
// hardening step.
//
// ---------------------------------------------------------------------------
// RESPONSE  (server -> client: zero or more DATA frames, then exactly one END)
// ---------------------------------------------------------------------------
//   DATA  0x01   len uint32, bytes[len]    raw grep stdout; may split a line
//   END   0x02   exit_code int32, line_count uint64
//
// The END frame is the part that cannot be simplified away. Without it, these
// three situations are byte-for-byte identical on the wire:
//
//   - grep exited 1, no lines matched          (a correct answer)
//   - grep exited 2, bad regex                 (a real error)
//   - the machine was killed before writing    (a dead peer)
//
// exit_code separates the first two. line_count, compared against the lines the
// client actually received, catches a peer that died mid-stream. And a stream
// that ends with no END frame at all is a peer that vanished.
//
// grep exit codes are passed through verbatim and are NOT all failures:
//   0 = one or more lines matched
//   1 = no lines matched  <-- a valid, successful answer
//   2 = a real error (bad regex, unreadable file, ...)
//
// ---------------------------------------------------------------------------
// DEFERRED TO v2 -- add these before the cluster runs and before Phase D
// ---------------------------------------------------------------------------
//   magic uint32 + version uint16 at the head of the request. Catches pointing
//     dgrep at the wrong port (a stale daemon, another service) instead of
//     parsing someone else's bytes as grep output.
//   kMaxArgc / kMaxArgLen / kMaxFrameLen guardrails. A length field is a number
//     the PEER chose; trust one that says 4 GB and you allocate 4 GB.
//     server_main.cpp requires the daemon survive a malformed request.
//   An ERROR frame carrying grep's stderr text, so MachineStatus::kGrepError
//     can report why rather than just that. Until then exit_code 2 with no
//     detail is the v1 answer.
//
#include <cstdint>
#include <string>
#include <vector>

namespace mp1 {

enum class FrameType : uint8_t {
    kData = 0x01,
    kEnd  = 0x02,
};

struct Request {
    std::vector<std::string> argv;  // grep options + pattern; NO file path
};

// ---------------------------------------------------------------------------
// Big-endian integer <-> byte helpers. Everything below is built from these, so
// write and test them first.
//
// Shift and mask; do NOT cast a pointer or memcpy the integer. A cast produces
// the machine's native order, which is little-endian on x86 -- and since both
// ends here are x86, it would be wrong on the wire in the same way at both ends
// and appear to work. PutU32(0x4D503143) must produce the bytes 4D 50 31 43,
// which spell "MP1C"; backwards, they spell "C1PM".
// ---------------------------------------------------------------------------

// TODO
void PutU32(uint32_t v, std::vector<uint8_t>& out);
void PutU64(uint64_t v, std::vector<uint8_t>& out);

// TODO: read from `p`. Bounds checking is the caller's job.
uint32_t GetU32(const uint8_t* p);
uint64_t GetU64(const uint8_t* p);

// ---------------------------------------------------------------------------
// Serialization. These operate on byte buffers only -- they do no I/O, which is
// what makes them straightforward to unit test locally without any sockets.
// ---------------------------------------------------------------------------

// TODO: append the encoded request to `out` per the layout above.
void EncodeRequest(const Request& req, std::vector<uint8_t>& out);

// TODO: parse a request from `buf`. Return false on anything malformed. Must
// not read past the end of the buffer even when the length fields lie -- a
// cursor that tracks the remaining byte count makes this structural instead of
// five hand-written bounds checks you have to remember.
bool DecodeRequest(const std::vector<uint8_t>& buf, Request& out);

// TODO: frame writers. Type byte, then the payload per the layout above.
void EncodeDataFrame(const uint8_t* data, size_t len, std::vector<uint8_t>& out);
void EncodeEndFrame(int32_t exit_code, uint64_t line_count,
                    std::vector<uint8_t>& out);

}  // namespace mp1
