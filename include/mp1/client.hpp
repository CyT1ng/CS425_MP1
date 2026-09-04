#pragma once
//
// The querier. Fans a single grep out to every machine in the config, in
// parallel, and merges what comes back.
//
// THE central performance decision, and the one the spec asks you to justify:
// we ship the QUERY to the data, never the data to the query. Each machine
// greps its own log locally and returns only matching lines.
//
// Why, concretely, at the demo's scale:
//   - Shipping logs: 60 MB x N over a ~1 Gbps link ~= 480 ms PER MACHINE of
//     pure transfer, before the querier has run a single byte of grep, and the
//     querier then greps N x 60 MB serially on one CPU.
//   - Shipping the query: grep reads 60 MB locally at GB/s (~40 ms warm), and
//     all N machines do it CONCURRENTLY. Only matches cross the network.
//   - The two are equal only if a pattern matches ~100% of lines, which is not
//     a real query. Local grep never loses.
//
// And note what this is NOT: there is no key space, no partitioning, no shuffle,
// no reduce phase. It is a scatter/gather fan-out RPC. The spec forbids
// MapReduce-like designs, so be ready to draw that distinction out loud.
//
// Concurrency requirement: one thread per machine. The reported metric is "time
// for the LAST machine to respond with the last byte", i.e. the MAX over
// machines. Querying serially would make it the SUM and would show up directly
// in your latency plot.
//
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "mp1/config.hpp"

namespace mp1 {

enum class MachineStatus {
    kOk,           // clean trailer received
    kNoMatch,      // clean trailer, grep exit 1 -- still a success
    kGrepError,    // grep exit 2; see stderr_text
    kUnreachable,  // connect refused or timed out; the machine is down
    kPartial,      // died mid-stream: bytes received but no trailer
    kProtocolError // peer spoke something we could not parse
};

struct MachineResult {
    Machine       machine;
    MachineStatus status = MachineStatus::kUnreachable;
    uint64_t      line_count = 0;
    std::string   error_text;
    std::chrono::milliseconds latency{0};  // per-machine, for the report
};

struct QueryOptions {
    std::chrono::milliseconds connect_timeout{2000};
    std::chrono::milliseconds total_deadline{60000};
    bool counts_only = false;  // suppress line output; print the summary only
};

struct QuerySummary {
    std::vector<MachineResult> results;
    uint64_t total_lines = 0;
    int      machines_ok = 0;
    int      machines_failed = 0;
    std::chrono::milliseconds wall_latency{0};  // <-- the number the report plots
};

// TODO: the fan-out.
//
//   1. Take t0 (steady_clock, not system_clock -- it must not jump).
//   2. Spawn one std::thread per machine. Each thread: connect with deadline,
//      send the request, drain frames, write output, record its own latency.
//   3. Join all threads. Take t1. wall_latency = t1 - t0, which is exactly the
//      spec's "last machine to respond with the last byte".
//   4. Print the per-machine summary: filename, line count, status.
//
// FAULT TOLERANCE -- the part the demo will actually poke at:
//   - A failed machine must NOT block the others. Because each machine has its
//     own thread and its own deadline, a dead peer costs you connect_timeout,
//     not the query.
//   - Report failures explicitly. Silently omitting a dead machine looks
//     identical to a machine that legitimately had zero matches, and the grader
//     cannot tell you got it right.
//   - Results from every live machine must still be complete and correct.
//
// OUTPUT ORDERING: many threads writing to stdout will interleave mid-line.
// Guard stdout with a mutex and write one whole chunk at a time. Every line is
// already prefixed with its filename, so interleaving between machines is
// correct -- interleaving WITHIN a line is corruption.
QuerySummary RunQuery(const std::vector<Machine>& machines,
                      const std::vector<std::string>& grep_args,
                      const QueryOptions& opts);

}  // namespace mp1
