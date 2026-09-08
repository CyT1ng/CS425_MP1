#include "mp1/protocol.hpp"

#include <sstream>

namespace mp1 {
namespace {

// Every length below is a number the PEER chose. These caps are checked before
// anything is allocated, so a malformed "1000000000" cannot make a daemon
// reserve a gigabyte on behalf of a stranger. They are far above any legitimate
// value: the longest real argument is a regex, and the server sends output in
// 64 KB chunks.
constexpr size_t kMaxArgs     = 256;
constexpr size_t kMaxArgLen   = 1u << 20;   // 1 MB
constexpr size_t kMaxChunkLen = 8u << 20;   // 8 MB

// ...and a budget for the request as a whole. Capping each argument alone would
// still let 256 of them at a megabyte each ask a daemon for a quarter of a
// gigabyte, once per connection.
constexpr size_t kMaxRequestBytes = 4u << 20;   // 4 MB

// Marks a message as a violation of the wire format rather than a dead socket.
// See kWireErrorPrefix in protocol.hpp for who reads the difference and why.
bool Malformed(std::string* err, const std::string& what) {
    if (err) *err = std::string(kWireErrorPrefix) + what;
    return false;
}

// Renders a header we did not understand so it can go in an error message.
// The bytes came from a stranger, so control characters are escaped and the
// whole thing is truncated -- an error message is not a reason to dump
// arbitrary input into someone's terminal.
std::string Quoted(const std::string& raw) {
    constexpr size_t kMaxShown = 40;
    std::string out = "\"";
    for (size_t i = 0; i < raw.size() && i < kMaxShown; ++i) {
        const unsigned char c = static_cast<unsigned char>(raw[i]);
        if (c >= 0x20 && c < 0x7f) {
            out += static_cast<char>(c);
        } else {
            out += '?';
        }
    }
    if (raw.size() > kMaxShown) out += "...";
    out += '"';
    return out;
}

// Parses a decimal length and refuses anything a peer could use against us.
// std::stoull is deliberately not trusted on its own: it happily accepts
// " 12 apples" and hands back 12. Digits only, and few enough of them that the
// conversion cannot overflow.
bool ParseLength(const std::string& text, size_t cap, size_t* out,
                 std::string* err) {
    constexpr size_t kMaxDigits = 12;  // 999,999,999,999 -- no overflow possible
    if (text.empty() || text.size() > kMaxDigits ||
        text.find_first_not_of("0123456789") != std::string::npos) {
        return Malformed(err, "expected a decimal length, got " + Quoted(text));
    }
    const unsigned long long value = std::stoull(text);
    if (value > cap) {
        return Malformed(err, "length " + text + " exceeds the " +
                                  std::to_string(cap) + " byte limit");
    }
    *out = static_cast<size_t>(value);
    return true;
}

// True when `line` begins with `prefix`, e.g. "D 4096" begins with "D ".
bool StartsWith(const std::string& line, const char* prefix) {
    return line.rfind(prefix, 0) == 0;
}

}  // namespace

// See protocol.hpp for the exact wire format. Everything here is one line of
// ASCII header plus, sometimes, a length-prefixed payload -- Conn does all the
// looping, so these functions stay short.

// --- client side ----------------------------------------------------------

bool SendRequest(Conn& conn, const Request& req, std::string* err) {
    // Built as one buffer and written once. A request is small, and a single
    // write keeps it inside a single TCP segment instead of a dozen tiny ones.
    std::string out = "ARGS " + std::to_string(req.argv.size()) + "\n";
    for (const std::string& arg : req.argv) {
        out += std::to_string(arg.size());
        out += '\n';
        out += arg;
        out += '\n';  // terminator, not a delimiter -- see RecvRequest
    }
    return conn.WriteAll(out, err);
}

bool RecvFrame(Conn& conn, Frame* out, std::string* err) {
    std::string header;
    if (!conn.ReadLine(&header, err)) return false;

    // "D <len>\n" plus that many bytes: a chunk of grep's stdout, which may
    // begin or end in the middle of a line.
    if (StartsWith(header, "D ")) {
        size_t len = 0;
        if (!ParseLength(header.substr(2), kMaxChunkLen, &len, err)) return false;
        if (!conn.ReadExactly(len, &out->data, err)) return false;
        out->is_end = false;
        return true;
    }

    // "E <exit_code> <line_count>\n": the end of the stream.
    if (StartsWith(header, "E ")) {
        std::istringstream fields(header.substr(2));
        int      exit_code  = 0;
        uint64_t line_count = 0;
        std::string trailing;
        if (!(fields >> exit_code >> line_count) || (fields >> trailing)) {
            return Malformed(err, "malformed END line " + Quoted(header));
        }
        out->is_end     = true;
        out->exit_code  = exit_code;
        out->line_count = line_count;
        return true;
    }

    // Neither -- most likely log-query was pointed at something that is not a
    // log-server at all. Saying so beats printing another service's bytes as if
    // they were grep output.
    return Malformed(err, "expected a D or E line, got " + Quoted(header) +
                              " (wrong port?)");
}

// --- server side ----------------------------------------------------------

bool RecvRequest(Conn& conn, Request* out, std::string* err) {
    std::string header;
    if (!conn.ReadLine(&header, err)) return false;
    if (!StartsWith(header, "ARGS ")) {
        return Malformed(err, "expected an ARGS line, got " + Quoted(header));
    }

    size_t argc = 0;
    if (!ParseLength(header.substr(5), kMaxArgs, &argc, err)) return false;

    out->argv.clear();
    out->argv.reserve(argc);
    size_t bytes_so_far = 0;
    for (size_t i = 0; i < argc; ++i) {
        std::string length_line;
        if (!conn.ReadLine(&length_line, err)) return false;

        size_t len = 0;
        if (!ParseLength(length_line, kMaxArgLen, &len, err)) return false;

        bytes_so_far += len;
        if (bytes_so_far > kMaxRequestBytes) {
            return Malformed(err, "request exceeds the " +
                                      std::to_string(kMaxRequestBytes) +
                                      " byte limit");
        }

        std::string arg;
        if (!conn.ReadExactly(len, &arg, err)) return false;

        // The length is what delimits the argument, so an argument may contain
        // newlines. The one that follows it is a terminator, and reading it
        // back as an EMPTY line proves the length was right: a wrong length
        // leaves us mid-argument here and fails on the spot, rather than
        // silently shifting every field after it.
        std::string terminator;
        if (!conn.ReadLine(&terminator, err)) return false;
        if (!terminator.empty()) {
            return Malformed(err, "argument " + std::to_string(i) +
                                      " is not followed by a newline; its "
                                      "declared length was wrong");
        }
        out->argv.push_back(std::move(arg));
    }
    return true;
}

bool SendData(Conn& conn, const std::string& chunk, std::string* err) {
    // An empty chunk carries no information; skipping it keeps the wire clean.
    if (chunk.empty()) return true;

    // Header and payload go out as two writes rather than one concatenation:
    // the payload can be 64 KB, and copying it just to prepend six bytes would
    // double the memory traffic on exactly the path that has to be fast.
    const std::string header = "D " + std::to_string(chunk.size()) + "\n";
    return conn.WriteAll(header, err) && conn.WriteAll(chunk, err);
}

bool SendEnd(Conn& conn, int exit_code, uint64_t line_count, std::string* err) {
    // Sent even when grep failed: without it the client cannot tell "nothing
    // matched" from "this machine is broken" from "this machine is dead".
    const std::string line = "E " + std::to_string(exit_code) + " " +
                             std::to_string(line_count) + "\n";
    return conn.WriteAll(line, err);
}

}  // namespace mp1
