#include "mp1/protocol.hpp"

namespace mp1 {

// See protocol.hpp for the byte layout. Write PutU32/GetU32 first and test them
// on their own -- everything else here is built from them, and an endianness
// bug at this level only shows up as garbage on the wire.

void PutU32(uint32_t v, std::vector<uint8_t>& out) {
    (void)v; (void)out;
    // TODO
}

void PutU64(uint64_t v, std::vector<uint8_t>& out) {
    (void)v; (void)out;
    // TODO
}

uint32_t GetU32(const uint8_t* p) {
    (void)p;
    return 0;  // TODO
}

uint64_t GetU64(const uint8_t* p) {
    (void)p;
    return 0;  // TODO
}

void EncodeRequest(const Request& req, std::vector<uint8_t>& out) {
    (void)req; (void)out;
    // TODO
}

bool DecodeRequest(const std::vector<uint8_t>& buf, Request& out) {
    (void)buf; (void)out;
    // TODO: argc, then each arg. Check every length actually fits inside buf.
    return false;
}

void EncodeDataFrame(const uint8_t* data, size_t len, std::vector<uint8_t>& out) {
    (void)data; (void)len; (void)out;
    // TODO
}

void EncodeEndFrame(int32_t exit_code, uint64_t line_count,
                    std::vector<uint8_t>& out) {
    (void)exit_code; (void)line_count; (void)out;
    // TODO
}

}  // namespace mp1
