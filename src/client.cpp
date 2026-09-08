#include "mp1/client.hpp"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <future>

#include "mp1/net.hpp"
#include "mp1/protocol.hpp"

namespace mp1 {
namespace {

// Column width for the summary table. "machine.10.log" is the longest name a
// ten-machine cluster produces.
constexpr int kNameWidth = 16;

// A machine that answered the question, whether or not it found anything.
// Everything else -- unreachable, cut off, confused -- is a machine whose
// answer we do not have, and the table has to say so rather than print a zero.
bool Answered(MachineStatus status) {
    return status == MachineStatus::kOk || status == MachineStatus::kNoMatch;
}

// Counts lines exactly the way RunGrep does on the server, so the two numbers
// are comparable: a final line without a trailing newline is still a line.
uint64_t CountLines(const std::string& text) {
    if (text.empty()) return 0;
    uint64_t lines = static_cast<uint64_t>(std::count(text.begin(), text.end(), '\n'));
    if (text.back() != '\n') ++lines;
    return lines;
}

// One conversation with one machine, writing its outcome into `out`.
//
// Split out of QueryOne so that every failure can simply return: the status is
// already recorded, and QueryOne stamps the latency once, on the way out.
//
// The rule the statuses follow, and it is worth being able to state at the
// demo: a failure BEFORE the connection is up is kUnreachable, a failure after
// it is up is kPartial, and a peer that answered with something that is not
// this protocol is kProtocolError.
void Exchange(const Machine& machine,
              const std::vector<std::string>& grep_args,
              const QueryOptions& opts,
              MachineResult* out) {
    std::string err;

    Conn conn = Connect(machine.host, machine.port, opts.connect_timeout, &err);
    if (!conn.valid()) {
        out->status     = MachineStatus::kUnreachable;
        out->error_text = err;
        return;
    }
    // From here on the peer is expected to keep talking. If it freezes instead
    // of dying, this timeout is what turns the query into an answer.
    if (!conn.SetReadTimeout(opts.read_timeout, &err) ||
        !SendRequest(conn, Request{grep_args}, &err)) {
        out->status     = MachineStatus::kUnreachable;
        out->error_text = err;
        return;
    }

    for (;;) {
        Frame frame;
        if (!RecvFrame(conn, &frame, &err)) {
            // The peer either stopped talking or said something that is not
            // this protocol; kWireErrorPrefix is how protocol.cpp reports the
            // second case. See its declaration in protocol.hpp.
            const bool malformed = err.rfind(kWireErrorPrefix, 0) == 0;
            out->status     = malformed ? MachineStatus::kProtocolError
                                        : MachineStatus::kPartial;
            out->error_text = err;
            return;
        }

        if (!frame.is_end) {
            out->output += frame.data;
            continue;
        }

        // The END line closes the stream and is what makes failure detectable.
        // Its line count is checked against the lines that actually arrived: a
        // machine killed halfway through sending is otherwise indistinguishable
        // from one that simply had less to say.
        const uint64_t received = CountLines(out->output);
        out->line_count = received;
        if (received != frame.line_count) {
            out->status     = MachineStatus::kPartial;
            out->error_text = "expected " + std::to_string(frame.line_count) +
                              " lines, received " + std::to_string(received);
            return;
        }

        switch (frame.exit_code) {
            case 0:  out->status = MachineStatus::kOk;       break;
            case 1:  out->status = MachineStatus::kNoMatch;  break;  // a valid answer
            default:
                out->status     = MachineStatus::kGrepError;
                out->error_text = "grep exited " + std::to_string(frame.exit_code) +
                                  " (see this machine's log-server output)";
                break;
        }
        return;
    }
}

}  // namespace

const char* StatusText(MachineStatus status) {
    switch (status) {
        case MachineStatus::kOk:            return "OK";
        case MachineStatus::kNoMatch:       return "NO MATCH";
        case MachineStatus::kGrepError:     return "GREP ERROR";
        case MachineStatus::kUnreachable:   return "UNREACHABLE";
        case MachineStatus::kPartial:       return "PARTIAL";
        case MachineStatus::kProtocolError: return "PROTOCOL ERROR";
    }
    return "UNKNOWN";  // only reachable via a cast; keeps the compiler happy
}

MachineResult QueryOne(const Machine& machine,
                       const std::vector<std::string>& grep_args,
                       const QueryOptions& opts) {
    MachineResult result;
    result.machine = machine;

    const auto started = std::chrono::steady_clock::now();
    try {
        Exchange(machine, grep_args, opts, &result);
    } catch (const std::exception& e) {
        // Nothing may escape this function. A machine failing is a normal
        // outcome recorded in `status`, never an exception that takes the whole
        // query down with it -- that is the fault-tolerance requirement,
        // expressed as a return type. The catch makes it true rather than
        // merely intended: a huge result set can still throw bad_alloc.
        result.status     = MachineStatus::kProtocolError;
        result.error_text = std::string("unexpected exception: ") + e.what();
    }
    result.latency = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    return result;
}

QuerySummary RunQuery(const std::vector<Machine>& machines,
                      const std::vector<std::string>& grep_args,
                      const QueryOptions& opts) {
    QuerySummary summary;
    const auto started = std::chrono::steady_clock::now();

    // Every machine is launched before any of them is waited on. That ordering
    // is the entire performance argument of this MP: the query costs the time
    // of the SLOWEST machine, not the sum of all ten.
    //
    // std::launch::async is not optional. Without it the implementation is free
    // to defer the work until get(), which would quietly make the fan-out
    // sequential -- the code would still be correct and the latency plot would
    // still be wrong.
    std::vector<std::future<MachineResult>> pending;
    pending.reserve(machines.size());
    for (const Machine& machine : machines) {
        pending.push_back(std::async(std::launch::async, QueryOne,
                                     std::cref(machine), std::cref(grep_args),
                                     std::cref(opts)));
    }

    // Collected in config order, so the table reads the same on every run and
    // on every machine. Waiting on a future IS the join; each task returns a
    // finished result by value, so there is no shared state and no lock here.
    summary.results.reserve(machines.size());
    for (std::future<MachineResult>& task : pending) {
        MachineResult result = task.get();
        if (Answered(result.status)) {
            ++summary.machines_ok;
            summary.total_lines += result.line_count;
        } else {
            // A partial answer's lines are printed but not totalled: the count
            // could not be verified, and a number that might be wrong is worse
            // than an obvious gap.
            ++summary.machines_failed;
        }
        summary.results.push_back(std::move(result));
    }

    // Stopped after the last machine's last byte -- exactly the latency the
    // spec asks the report to plot. steady_clock, not system_clock, so an NTP
    // correction mid-query cannot produce a negative measurement.
    summary.wall_latency = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    return summary;
}

void PrintSummary(const QuerySummary& summary, const QueryOptions& opts) {
    // The matching lines first, machine by machine in config order. Each line
    // already carries its own "machine.<i>.log:" prefix from grep -H, so the
    // blocks stay readable however they are ordered.
    if (!opts.counts_only) {
        for (const MachineResult& result : summary.results) {
            std::fwrite(result.output.data(), 1, result.output.size(), stdout);
        }
    }

    // Then the per-machine table. The per-file count with its filename is a
    // hard spec requirement, and a failed machine gets a WORD where its count
    // would be -- printing 0 for a machine that is down, or leaving it out
    // altogether, is indistinguishable from one that legitimately had no match.
    for (const MachineResult& result : summary.results) {
        const std::string name = LogFileName(result.machine.id);
        const long long   ms   = static_cast<long long>(result.latency.count());
        if (Answered(result.status)) {
            std::printf("%-*s %8llu lines   (%lld ms)\n", kNameWidth, name.c_str(),
                        static_cast<unsigned long long>(result.line_count), ms);
        } else {
            std::printf("%-*s %8s %s   (%lld ms)\n", kNameWidth, name.c_str(),
                        "--", StatusText(result.status), ms);
        }
    }

    std::printf("%s\n", std::string(52, '-').c_str());
    std::printf("%-*s %8llu lines from %d/%zu machines   (%lld ms)\n",
                kNameWidth, "total",
                static_cast<unsigned long long>(summary.total_lines),
                summary.machines_ok, summary.results.size(),
                static_cast<long long>(summary.wall_latency.count()));

    // One machine-readable line, so scripts/measure.sh does not have to scrape
    // the table above -- which leaves the table free to stay human-readable.
    std::printf("SUMMARY latency_ms=%lld total_lines=%llu machines_ok=%d "
                "machines_failed=%d\n",
                static_cast<long long>(summary.wall_latency.count()),
                static_cast<unsigned long long>(summary.total_lines),
                summary.machines_ok, summary.machines_failed);

    // Why each failure happened goes to stderr, so a pipeline reading stdout
    // gets the results and a human still gets told what went wrong.
    for (const MachineResult& result : summary.results) {
        if (result.error_text.empty()) continue;
        std::fprintf(stderr, "log-query: %s: %s: %s\n",
                     LogFileName(result.machine.id).c_str(),
                     StatusText(result.status), result.error_text.c_str());
    }
}

int QueryExitCode(const QuerySummary& summary) {
    // Failures win: a query that missed a machine has not answered the
    // question, even if the machines it did reach found something.
    if (summary.machines_failed > 0) return 2;
    for (const MachineResult& result : summary.results) {
        if (result.status == MachineStatus::kOk) return 0;
    }
    return 1;
}

}  // namespace mp1
