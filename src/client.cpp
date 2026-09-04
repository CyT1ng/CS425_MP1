#include "mp1/client.hpp"

#include <mutex>
#include <thread>

#include "mp1/net.hpp"
#include "mp1/protocol.hpp"

namespace mp1 {

// TODO: one thread per machine, joined at the end. See client.hpp for the full
// contract, the fault-tolerance requirements, and the stdout locking rule.
//
// Per-thread shape:
//   t_start = Clock::now()
//   fd = ConnectWithDeadline(...)          -> kUnreachable on failure
//   WriteFull(EncodeRequest(args))
//   loop: read 1-byte frame type, then the frame body
//         kData    -> lock stdout, write chunk, unlock; tally lines
//         kTrailer -> compare trailer count to lines actually received;
//                     equal => kOk/kNoMatch, otherwise kPartial
//         kError   -> kGrepError, keep stderr text
//         EOF before any trailer -> kPartial (peer died mid-stream)
//   result.latency = Clock::now() - t_start

QuerySummary RunQuery(const std::vector<Machine>& machines,
                      const std::vector<std::string>& grep_args,
                      const QueryOptions& opts) {
    (void)machines; (void)grep_args; (void)opts;
    return {};  // TODO
}

}  // namespace mp1
