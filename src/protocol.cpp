#include "mp1/protocol.hpp"

namespace mp1 {

// See protocol.hpp for the exact wire format. Everything here is one line of
// ASCII header plus, sometimes, a length-prefixed payload -- Conn does all the
// looping, so these functions stay short.

bool SendRequest(Conn& conn, const Request& req, std::string* err) {
    (void)conn; (void)req; (void)err;
    // TODO: "ARGS <n>\n", then per argument "<len>\n" followed by the bytes.
    return false;
}

bool RecvFrame(Conn& conn, Frame* out, std::string* err) {
    (void)conn; (void)out; (void)err;
    // TODO: ReadLine, then switch on the first character: 'D' -> read the
    // length and that many bytes; 'E' -> parse exit code and line count.
    // Anything else is a protocol error, and saying so is what turns a
    // wrong-port connection into a clear message instead of garbage output.
    return false;
}

bool RecvRequest(Conn& conn, Request* out, std::string* err) {
    (void)conn; (void)out; (void)err;
    // TODO: the mirror of SendRequest. This parses bytes a stranger sent you,
    // so validate the header rather than trusting it.
    return false;
}

bool SendData(Conn& conn, const std::string& chunk, std::string* err) {
    (void)conn; (void)chunk; (void)err;
    // TODO: "D <len>\n" then the bytes.
    return false;
}

bool SendEnd(Conn& conn, int exit_code, uint64_t line_count, std::string* err) {
    (void)conn; (void)exit_code; (void)line_count; (void)err;
    // TODO: "E <exit_code> <line_count>\n". Send it even when grep failed.
    return false;
}

}  // namespace mp1
