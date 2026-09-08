#pragma once
//
// The fan-out: query every machine at once and collect the answers.
//
// This is the one place where the design decision the spec asks you to justify
// actually lives. Ten machines are queried CONCURRENTLY, so the query costs the
// time of the SLOWEST machine, not the sum of all ten. Query them one after
// another and your latency plot shows it immediately.
//
// How the concurrency is expressed here: one std::async task per machine, each
// returning a finished MachineResult by value. No shared mutable state between
// tasks, so there are no locks anywhere in this file, and no thread handles to
// track and join by hand. Waiting on a future is the join.
//
// The cost of that choice, stated plainly so you can defend it: each task
// buffers its machine's matching lines in memory before returning them, so a
// frequent pattern over ten 60 MB logs can hold a few hundred MB. That is a
// deliberate trade of memory for readability while you are learning. The fix,
// when you want it, is to stream chunks out as they arrive under a stdout
// mutex -- at which point output from different machines interleaves and you
// need the filename prefix on every line to keep it sensible.
//
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "mp1/config.hpp"

namespace mp1 {

enum class MachineStatus {
    kOk,            // END received, grep matched something
    kNoMatch,       // END received, grep exit 1 -- still a success
    kGrepError,     // END received, grep exit 2
    kUnreachable,   // could not connect: the machine is down
    kPartial,       // died mid-stream: bytes arrived but no END line
    kProtocolError, // the peer said something we could not parse
};

// "OK" / "NO MATCH" / "UNREACHABLE" / ... for the summary table.
const char* StatusText(MachineStatus status);

struct MachineResult {
    Machine       machine;
    MachineStatus status = MachineStatus::kUnreachable;
    std::string   output;                        // grep's matching lines
    uint64_t      line_count = 0;
    std::string   error_text;                    // why, when something failed
    std::chrono::milliseconds latency{0};        // this machine alone
};

struct QueryOptions {
    std::chrono::milliseconds connect_timeout{2000};
    std::chrono::milliseconds read_timeout{60000};
    bool counts_only = false;   // suppress the lines, print the summary only
};

struct QuerySummary {
    std::vector<MachineResult> results;      // in config order, always
    uint64_t total_lines    = 0;
    int      machines_ok    = 0;
    int      machines_failed = 0;
    std::chrono::milliseconds wall_latency{0};   // the number the report plots
};

// Queries one machine: connect, SendRequest, then RecvFrame until the END line,
// filling in a MachineResult. This function NEVER throws or propagates a
// failure upward -- a dead machine is a normal outcome recorded in `status`,
// not an error that aborts the query. That is the whole fault-tolerance
// requirement, expressed as a return type.
//
// The rule the statuses follow, since the demo exercises all of it:
//   - could not connect                  -> kUnreachable
//   - connected, then the stream stopped  -> kPartial, caught by comparing the
//     END line's count against the lines that actually arrived
//   - answered, but not in this protocol  -> kProtocolError
MachineResult QueryOne(const Machine& machine,
                       const std::vector<std::string>& grep_args,
                       const QueryOptions& opts);

// The fan-out:
//   1. t0 = steady_clock::now()   (steady, not system -- it must not jump)
//   2. one std::async(std::launch::async, QueryOne, ...) per machine, ALL
//      launched before any of them is waited on
//   3. every future collected in config order; t1 after the last one
//   4. wall_latency = t1 - t0, which is exactly the spec's "time for the last
//      machine to respond with the last byte"
//
// std::launch::async is not optional. Without it the implementation is allowed
// to defer the task and run it when get() is called -- which would silently
// make the fan-out sequential and quietly wreck the latency numbers.
QuerySummary RunQuery(const std::vector<Machine>& machines,
                      const std::vector<std::string>& grep_args,
                      const QueryOptions& opts);

// Prints the matching lines (unless opts.counts_only), then the per-machine
// table and the totals:
//
//   machine.1.log      1423 lines   (41 ms)
//   machine.2.log         0 lines   (38 ms)
//   machine.3.log           -- UNREACHABLE   (2001 ms)
//   ----------------------------------------------------
//   total              1423 lines from 2/3 machines   (412 ms)
//   SUMMARY latency_ms=412 total_lines=1423 machines_ok=2 machines_failed=1
//
// Per-file counts with the filename are a hard spec requirement. Showing
// UNREACHABLE distinctly from a zero count is what proves the fault tolerance
// works -- silently dropping a dead machine looks identical to one that
// legitimately had no matches.
//
// Results go to stdout and the reasons behind failures go to stderr, so a
// script can read one while a human reads the other. The SUMMARY line exists so
// scripts/measure.sh never has to parse the table's spacing.
void PrintSummary(const QuerySummary& summary, const QueryOptions& opts);

// The process exit status for a finished query, mirroring grep so that
// log-query composes in a shell pipeline:
//   0 = at least one machine matched
//   1 = no matches anywhere, and nothing failed
//   2 = a grep error, or a machine that could not be reached
//
// It lives here rather than in client_main.cpp so the test suite can assert on
// the same rule the binary actually uses.
int QueryExitCode(const QuerySummary& summary);

}  // namespace mp1
