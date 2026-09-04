#include "mp1/net.hpp"

#include <sys/socket.h>
#include <sys/types.h>

namespace mp1 {

// TODO: implement all of these. Read the pitfalls listed in net.hpp first --
// partial reads, EINTR, and unbounded blocking are the three that will cost you
// a working demo.
//
// A useful shared helper: given a deadline, compute the remaining milliseconds
// for poll(). Clamp at 0 (already expired) and return -1 only if you genuinely
// want to block forever, which here you never do.

int ConnectWithDeadline(const std::string& host, uint16_t port,
                        TimePoint deadline, std::string* err) {
    (void)host; (void)port; (void)deadline;
    if (err) *err = "ConnectWithDeadline not implemented";
    return -1;
}

int Listen(uint16_t port, std::string* err) {
    (void)port;
    if (err) *err = "Listen not implemented";
    return -1;
}

bool ReadFull(int fd, uint8_t* buf, size_t len, TimePoint deadline) {
    (void)fd; (void)buf; (void)len; (void)deadline;
    return false;  // TODO
}

bool WriteFull(int fd, const uint8_t* buf, size_t len, TimePoint deadline) {
    (void)fd; (void)buf; (void)len; (void)deadline;
    return false;  // TODO
}

ssize_t ReadSome(int fd, uint8_t* buf, size_t len, TimePoint deadline) {
    (void)fd; (void)buf; (void)len; (void)deadline;
    return -1;  // TODO
}

}  // namespace mp1
