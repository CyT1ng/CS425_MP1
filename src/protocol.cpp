#include "mp1/protocol.hpp"

namespace mp1 {

// TODO: see protocol.hpp for the exact byte layout.
// Suggestion: write tiny PutU16/PutU32/PutU64 + GetU* helpers first and test
// them in isolation. Hand-rolled shifting inline at each call site is where
// endianness bugs hide, and those only show up as garbage on the wire.

void EncodeRequest(const Request& req, std::vector<uint8_t>& out) {
    (void)req; (void)out;
    // TODO
}

bool DecodeRequest(const std::vector<uint8_t>& buf, Request& out) {
    (void)buf; (void)out;
    // TODO: validate magic, version, argc <= kMaxArgc, every len <= kMaxArgLen,
    // and that the declared lengths actually fit inside buf.
    return false;
}

void EncodeDataFrame(const uint8_t* data, size_t len, std::vector<uint8_t>& out) {
    (void)data; (void)len; (void)out;
    // TODO
}

void EncodeTrailerFrame(int32_t exit_code, uint64_t line_count,
                        std::vector<uint8_t>& out) {
    (void)exit_code; (void)line_count; (void)out;
    // TODO
}

void EncodeErrorFrame(const std::string& message, std::vector<uint8_t>& out) {
    (void)message; (void)out;
    // TODO
}

}  // namespace mp1
