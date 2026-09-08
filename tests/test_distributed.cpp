// Distributed unit tests -- Part II of the spec.
//
// The spec's minimum bar, quoted: "generates log files at every machine with
// some known lines and other random lines. The log-querying program then runs
// multiple greps and verifies automatically that the results are what you
// expect. You should use query patterns that are rare, frequent, and somewhat
// frequent, and patterns that occur in one/some/all logs."
//
// Every one of those axes has a named test below. The rest exercise fault
// tolerance and the code paths that only appear once real sockets are involved.
//
// All of these run against a local Cluster (N processes on 127.0.0.1), so the
// whole suite is one command with no manual intervention, on a laptop or a VM.

#include "harness.hpp"
#include "test_framework.hpp"

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <future>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "mp1/client.hpp"
#include "mp1/grep_runner.hpp"
#include "mp1/log_gen.hpp"
#include "mp1/net.hpp"
#include "mp1/protocol.hpp"

namespace {

// Each cluster gets its own block of ports, so a daemon that somehow outlives
// its test cannot make the next one fail in a way that looks like a real bug.
uint16_t NextBasePort() {
    static uint16_t next = 19000;
    const uint16_t base = next;
    next = static_cast<uint16_t>(next + 32);
    return base;
}

// Short enough that a hang shows up as a failure rather than as a long pause,
// generous enough not to fire on a loaded laptop.
mp1::QueryOptions TestOptions() {
    mp1::QueryOptions opts;
    opts.connect_timeout = std::chrono::milliseconds(1000);
    opts.read_timeout    = std::chrono::milliseconds(15000);
    return opts;
}

const mp1::MachineResult* ResultFor(const mp1::QuerySummary& summary, int machine_id) {
    for (const mp1::MachineResult& result : summary.results) {
        if (result.machine.id == machine_id) return &result;
    }
    return nullptr;
}

// Status assertions go through StatusText so a failure prints "PARTIAL" rather
// than an enumerator number nobody can read.
std::string StatusOf(const mp1::QuerySummary& summary, int machine_id) {
    const mp1::MachineResult* result = ResultFor(summary, machine_id);
    return result == nullptr ? "MISSING" : mp1::StatusText(result->status);
}

uint64_t LinesFrom(const mp1::QuerySummary& summary, int machine_id) {
    const mp1::MachineResult* result = ResultFor(summary, machine_id);
    return result == nullptr ? 0 : result->line_count;
}

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        const size_t newline = text.find('\n', start);
        if (newline == std::string::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, newline - start));
        start = newline + 1;
    }
    return lines;
}

// Every line the whole cluster returned, as a set.
std::set<std::string> AllLines(const mp1::QuerySummary& summary) {
    std::set<std::string> lines;
    for (const mp1::MachineResult& result : summary.results) {
        for (std::string& line : SplitLines(result.output)) {
            lines.insert(std::move(line));
        }
    }
    return lines;
}

// The same grep, run locally over the same generated file: the independent
// answer that a distributed result is checked against.
std::string LocalGrep(const std::string& log_path,
                      const std::vector<std::string>& args) {
    std::string output;
    mp1::GrepResult result;
    std::string err;
    mp1::RunGrep(args, log_path,
                 [&output](const std::string& chunk) {
                     output += chunk;
                     return true;
                 },
                 &result, &err);
    return output;
}

// Asserts that every machine reported exactly what the generator planted there.
// Machine by machine, never just the total: a total that happens to add up
// while the individual attributions are wrong is a real bug, and a total-only
// assertion cannot see it.
void CheckEveryMachine(const mp1test::Cluster& cluster,
                       const mp1::QuerySummary& summary,
                       const std::string& token) {
    for (const mp1::Machine& machine : cluster.machines()) {
        const uint64_t expected = cluster.ExpectedOn(token, machine.id);
        CHECK_EQ(LinesFrom(summary, machine.id), expected);
        CHECK_EQ(StatusOf(summary, machine.id),
                 std::string(expected > 0 ? "OK" : "NO MATCH"));
    }
    CHECK_EQ(summary.total_lines, cluster.ExpectedTotal(token));
}

// A stand-in for a machine that dies in the middle of answering.
//
// A real SIGKILL mid-stream is a race: a 2 MB answer is gone in milliseconds,
// so the signal lands before or after the transfer far more often than during
// it, and a test built on that would fail one run in ten for no reason. This
// peer reproduces the exact wire behaviour a killed daemon produces, every
// time -- some data, and then either silence or an END line promising more
// lines than it actually sent.
class BrokenPeer {
public:
    bool Start(uint16_t port, bool send_end, std::string* err) {
        listen_fd_ = mp1::Listen(port, err);
        if (listen_fd_ < 0) return false;

        worker_ = std::thread([this, send_end] {
            std::string err;
            mp1::Conn conn = mp1::Accept(listen_fd_, &err);
            if (!conn.valid()) return;

            mp1::Request request;
            if (!mp1::RecvRequest(conn, &request, &err)) return;

            mp1::SendData(conn, "machine.1.log:first line\n", &err);
            mp1::SendData(conn, "machine.1.log:second line\n", &err);
            if (send_end) {
                // Ten lines promised, two delivered. Comparing the two is what
                // catches a peer that stopped early but still said goodbye.
                mp1::SendEnd(conn, 0, 10, &err);
            }
            // Otherwise it just closes, which is what a killed daemon does.
        });
        return true;
    }

    void Stop() {
        if (worker_.joinable()) worker_.join();
        if (listen_fd_ >= 0) ::close(listen_fd_);
        listen_fd_ = -1;
    }

    ~BrokenPeer() { Stop(); }

private:
    int         listen_fd_ = -1;
    std::thread worker_;
};

}  // namespace

// --- the required frequency axis -----------------------------------------
//
// Rare / somewhat frequent / frequent, with the frequencies defined in
// log_gen.hpp so that these tests, the log generator and scripts/measure.sh all
// mean the same thing by the words.

TEST(Distributed_RarePattern) {
    mp1test::Cluster cluster;
    std::string err;
    REQUIRE(cluster.Start(5, NextBasePort(), &err), err);

    const mp1::QuerySummary summary =
        mp1::RunQuery(cluster.machines(), {mp1::kRareToken}, TestOptions());

    CheckEveryMachine(cluster, summary, mp1::kRareToken);
    CHECK(summary.total_lines > 0);          // rare means rare, not absent
    CHECK_EQ(summary.machines_ok, 5);
    CHECK_EQ(summary.machines_failed, 0);
    CHECK_EQ(mp1::QueryExitCode(summary), 0);
}

TEST(Distributed_SomewhatFrequentPattern) {
    mp1test::Cluster cluster;
    std::string err;
    REQUIRE(cluster.Start(5, NextBasePort(), &err), err);

    const mp1::QuerySummary summary =
        mp1::RunQuery(cluster.machines(), {mp1::kSomewhatToken}, TestOptions());

    CheckEveryMachine(cluster, summary, mp1::kSomewhatToken);
    CHECK_EQ(summary.machines_failed, 0);
}

TEST(Distributed_FrequentPattern) {
    mp1test::Cluster cluster;
    std::string err;
    REQUIRE(cluster.Start(5, NextBasePort(), &err), err);

    // Ten per cent of every log, so the answer spans many DATA frames and
    // several megabytes. This is the case that actually exercises the streaming
    // reader and its buffer boundaries -- a framing bug that a rare pattern
    // never reaches shows up here.
    const mp1::QuerySummary summary =
        mp1::RunQuery(cluster.machines(), {mp1::kFrequentToken}, TestOptions());

    CheckEveryMachine(cluster, summary, mp1::kFrequentToken);
    CHECK(summary.total_lines > 5000);
    CHECK_EQ(summary.machines_failed, 0);
}

// --- the required distribution axis --------------------------------------

TEST(Distributed_PatternInExactlyOneLog) {
    mp1test::Cluster cluster;
    std::string err;
    REQUIRE(cluster.Start(5, NextBasePort(), &err), err);

    // Planted on machine 3 alone. The point of the test is the OTHER four: they
    // must each report zero with a status of NO MATCH -- not unreachable, and
    // not left out of the summary, either of which would be a machine that
    // looks failed when it answered perfectly well.
    const mp1::QuerySummary summary =
        mp1::RunQuery(cluster.machines(), {mp1::kOneLogToken}, TestOptions());

    CHECK(cluster.ExpectedOn(mp1::kOneLogToken, 3) > 0);
    CheckEveryMachine(cluster, summary, mp1::kOneLogToken);
    CHECK_EQ(summary.results.size(), size_t{5});
    CHECK_EQ(summary.total_lines, cluster.ExpectedOn(mp1::kOneLogToken, 3));
    CHECK_EQ(summary.machines_ok, 5);
}

TEST(Distributed_PatternInSomeLogs) {
    mp1test::Cluster cluster;
    std::string err;
    REQUIRE(cluster.Start(5, NextBasePort(), &err), err);

    // Planted on the odd-numbered machines only. The exact SET of machines
    // reporting hits is asserted, not just the total.
    const mp1::QuerySummary summary =
        mp1::RunQuery(cluster.machines(), {mp1::kSomeLogsToken}, TestOptions());

    CheckEveryMachine(cluster, summary, mp1::kSomeLogsToken);
    for (const mp1::Machine& machine : cluster.machines()) {
        const bool should_have_hits = (machine.id % 2 == 1);
        CHECK_EQ(LinesFrom(summary, machine.id) > 0, should_have_hits);
    }
}

TEST(Distributed_PatternInAllLogs) {
    mp1test::Cluster cluster;
    std::string err;
    REQUIRE(cluster.Start(5, NextBasePort(), &err), err);

    const mp1::QuerySummary summary =
        mp1::RunQuery(cluster.machines(), {mp1::kSomewhatToken}, TestOptions());

    CheckEveryMachine(cluster, summary, mp1::kSomewhatToken);
    for (const mp1::Machine& machine : cluster.machines()) {
        CHECK(LinesFrom(summary, machine.id) > 0);
    }
}

TEST(Distributed_PatternInNoLog) {
    mp1test::Cluster cluster;
    std::string err;
    REQUIRE(cluster.Start(5, NextBasePort(), &err), err);

    // Nothing anywhere is a successful query with an empty answer, and it has
    // to be distinguishable from a query that failed: exit code 1, and not one
    // machine marked as having failed.
    const mp1::QuerySummary summary =
        mp1::RunQuery(cluster.machines(), {mp1::kAbsentToken}, TestOptions());

    CHECK_EQ(summary.total_lines, uint64_t{0});
    CHECK_EQ(summary.machines_ok, 5);
    CHECK_EQ(summary.machines_failed, 0);
    CHECK_EQ(mp1::QueryExitCode(summary), 1);
    for (const mp1::Machine& machine : cluster.machines()) {
        CHECK_EQ(StatusOf(summary, machine.id), std::string("NO MATCH"));
    }
}

// --- output correctness ---------------------------------------------------

TEST(Distributed_OutputLinesMatchLocalGroundTruth) {
    mp1test::Cluster cluster;
    std::string err;
    REQUIRE(cluster.Start(4, NextBasePort(), &err), err);

    // The strongest assertion in the suite: not "the counts agree" but "these
    // are the same lines". Counts can coincide while the content is wrong --
    // an off-by-one in the framing, a chunk dropped at a buffer boundary -- and
    // only comparing the text itself catches that.
    const mp1::QuerySummary summary =
        mp1::RunQuery(cluster.machines(), {mp1::kSomewhatToken}, TestOptions());
    const std::set<std::string> from_cluster = AllLines(summary);

    std::set<std::string> from_local_grep;
    for (const mp1::Machine& machine : cluster.machines()) {
        const std::string path =
            cluster.log_dir() + "/" + mp1::LogFileName(machine.id);
        for (std::string& line : SplitLines(LocalGrep(path, {mp1::kSomewhatToken}))) {
            from_local_grep.insert(std::move(line));
        }
    }

    CHECK(!from_local_grep.empty());
    CHECK_EQ(from_cluster.size(), from_local_grep.size());
    CHECK(from_cluster == from_local_grep);
}

TEST(Distributed_EveryLineIsPrefixedWithItsFilename) {
    mp1test::Cluster cluster;
    std::string err;
    REQUIRE(cluster.Start(4, NextBasePort(), &err), err);

    const mp1::QuerySummary summary =
        mp1::RunQuery(cluster.machines(), {mp1::kFrequentToken}, TestOptions());

    // The spec requires the filename on the output, so it is asserted rather
    // than trusted to -H. The id in the prefix also has to be the machine that
    // actually sent the line: a daemon serving the wrong log would otherwise
    // pass every count-based test in this file.
    size_t checked = 0;
    for (const mp1::MachineResult& result : summary.results) {
        const std::string prefix = mp1::LogFileName(result.machine.id) + ":";
        for (const std::string& line : SplitLines(result.output)) {
            if (line.rfind(prefix, 0) != 0) {
                CHECK(line.rfind(prefix, 0) == 0);
                std::printf("  offending line: %s\n", line.substr(0, 60).c_str());
                return;
            }
            ++checked;
        }
    }
    CHECK(checked > 0);
}

TEST(Distributed_LinesAreNeverInterleavedMidLine) {
    mp1test::Cluster cluster;
    std::string err;
    REQUIRE(cluster.Start(5, NextBasePort(), &err), err);

    // A frequent query across five machines: megabytes of output, arriving
    // concurrently. Every line must still parse as "machine.N.log:<rest>" with
    // N a machine in this cluster. A torn line -- one machine's bytes spliced
    // into another's -- only ever shows up under a large multi-machine result,
    // which is exactly this.
    const mp1::QuerySummary summary =
        mp1::RunQuery(cluster.machines(), {mp1::kFrequentToken}, TestOptions());

    std::set<std::string> valid_prefixes;
    for (const mp1::Machine& machine : cluster.machines()) {
        valid_prefixes.insert(mp1::LogFileName(machine.id));
    }

    size_t checked = 0;
    for (const std::string& line : AllLines(summary)) {
        const size_t colon = line.find(':');
        if (colon == std::string::npos ||
            valid_prefixes.count(line.substr(0, colon)) == 0) {
            CHECK(colon != std::string::npos);
            std::printf("  torn or unattributed line: %s\n",
                        line.substr(0, 60).c_str());
            return;
        }
        ++checked;
    }
    CHECK(checked > 1000);
}

// --- fault tolerance ------------------------------------------------------

TEST(FaultTolerance_QuerySucceedsWithOneMachineDown) {
    mp1test::Cluster cluster;
    std::string err;
    REQUIRE(cluster.Start(5, NextBasePort(), &err), err);
    REQUIRE(cluster.Kill(2, &err), err);

    // The spec's core requirement: "it should fetch answers from all machines
    // that have not failed". The live machines' answers must be complete and
    // correct, and machine 2 must be REPORTED, not quietly dropped.
    const mp1::QuerySummary summary =
        mp1::RunQuery(cluster.machines(), {mp1::kSomewhatToken}, TestOptions());

    CHECK_EQ(StatusOf(summary, 2), std::string("UNREACHABLE"));
    CHECK_EQ(summary.machines_ok, 4);
    CHECK_EQ(summary.machines_failed, 1);
    CHECK_EQ(summary.results.size(), size_t{5});   // still five rows in the table

    uint64_t expected_from_survivors = 0;
    for (const mp1::Machine& machine : cluster.machines()) {
        if (machine.id == 2) continue;
        const uint64_t expected = cluster.ExpectedOn(mp1::kSomewhatToken, machine.id);
        CHECK_EQ(LinesFrom(summary, machine.id), expected);
        expected_from_survivors += expected;
    }
    CHECK_EQ(summary.total_lines, expected_from_survivors);
    CHECK_EQ(mp1::QueryExitCode(summary), 2);
}

TEST(FaultTolerance_QuerySucceedsWithMostMachinesDown) {
    mp1test::Cluster cluster;
    std::string err;
    REQUIRE(cluster.Start(5, NextBasePort(), &err), err);
    for (int id : {2, 3, 4, 5}) REQUIRE(cluster.Kill(id, &err), err);

    // One survivor out of five, and its answer still has to be exact.
    const mp1::QuerySummary summary =
        mp1::RunQuery(cluster.machines(), {mp1::kFrequentToken}, TestOptions());

    CHECK_EQ(summary.machines_ok, 1);
    CHECK_EQ(summary.machines_failed, 4);
    CHECK_EQ(LinesFrom(summary, 1), cluster.ExpectedOn(mp1::kFrequentToken, 1));
    CHECK_EQ(summary.total_lines, cluster.ExpectedOn(mp1::kFrequentToken, 1));
}

TEST(FaultTolerance_AllMachinesDown) {
    mp1test::Cluster cluster;
    std::string err;
    REQUIRE(cluster.Start(4, NextBasePort(), &err), err);
    for (int id : {1, 2, 3, 4}) REQUIRE(cluster.Kill(id, &err), err);

    // No crash, no hang, and an answer that says plainly that nothing answered.
    const mp1::QuerySummary summary =
        mp1::RunQuery(cluster.machines(), {mp1::kSomewhatToken}, TestOptions());

    CHECK_EQ(summary.machines_ok, 0);
    CHECK_EQ(summary.machines_failed, 4);
    CHECK_EQ(summary.total_lines, uint64_t{0});
    CHECK_EQ(mp1::QueryExitCode(summary), 2);
    for (const mp1::Machine& machine : cluster.machines()) {
        CHECK_EQ(StatusOf(summary, machine.id), std::string("UNREACHABLE"));
    }
}

TEST(FaultTolerance_DeadMachineDoesNotDelayLiveOnes) {
    mp1test::Cluster cluster;
    std::string err;
    REQUIRE(cluster.Start(1, NextBasePort(), &err), err);

    // Four machines that are not merely down but GONE: 192.0.2.x is TEST-NET-1
    // (RFC 5737), reserved for documentation and routed nowhere, so a
    // connection attempt gets no answer at all. Localhost would refuse
    // instantly and prove nothing about the timeout.
    //
    // The assertion is that four dead machines cost ONE timeout, not four --
    // which is only true if the fan-out is really concurrent. A sequential
    // implementation fails this test loudly instead of quietly producing bad
    // numbers in the report.
    std::vector<mp1::Machine> machines = cluster.machines();
    for (int i = 1; i <= 4; ++i) {
        machines.push_back(mp1::Machine{100 + i, "192.0.2." + std::to_string(i), 4425});
    }

    mp1::QueryOptions opts = TestOptions();
    opts.connect_timeout = std::chrono::milliseconds(1000);
    opts.read_timeout    = std::chrono::milliseconds(2000);

    const mp1::QuerySummary summary =
        mp1::RunQuery(machines, {mp1::kSomewhatToken}, opts);

    CHECK(summary.wall_latency < std::chrono::milliseconds(3500));
    if (summary.wall_latency >= std::chrono::milliseconds(3500)) {
        std::printf("  wall latency was %lld ms for a 1000 ms connect timeout\n",
                    static_cast<long long>(summary.wall_latency.count()));
    }
    CHECK_EQ(summary.machines_ok, 1);
    CHECK_EQ(summary.machines_failed, 4);
    CHECK_EQ(LinesFrom(summary, 1), cluster.ExpectedOn(mp1::kSomewhatToken, 1));
}

TEST(FaultTolerance_MachineKilledMidStream) {
    const uint16_t base = NextBasePort();

    // Two ways a machine can die once it has started answering: it stops
    // without an END line, or it manages an END line that promises more than it
    // sent. Both have to be reported as PARTIAL -- never silently truncated
    // into a plausible-looking wrong count, which is the failure this whole
    // trailer design exists to prevent.
    BrokenPeer went_silent, promised_more;
    std::string err;
    REQUIRE(went_silent.Start(static_cast<uint16_t>(base + 1), false, &err), err);
    REQUIRE(promised_more.Start(static_cast<uint16_t>(base + 2), true, &err), err);

    const std::vector<mp1::Machine> machines = {
        mp1::Machine{1, "127.0.0.1", static_cast<uint16_t>(base + 1)},
        mp1::Machine{2, "127.0.0.1", static_cast<uint16_t>(base + 2)},
    };
    const mp1::QuerySummary summary =
        mp1::RunQuery(machines, {mp1::kSomewhatToken}, TestOptions());

    went_silent.Stop();
    promised_more.Stop();

    CHECK_EQ(StatusOf(summary, 1), std::string("PARTIAL"));
    CHECK_EQ(StatusOf(summary, 2), std::string("PARTIAL"));
    CHECK_EQ(summary.machines_failed, 2);
    CHECK_EQ(summary.total_lines, uint64_t{0});  // an unverified count is not a count
    CHECK_EQ(mp1::QueryExitCode(summary), 2);
}

TEST(FaultTolerance_DaemonSurvivesMalformedRequest) {
    mp1test::Cluster cluster;
    std::string err;
    REQUIRE(cluster.Start(3, NextBasePort(), &err), err);
    const mp1::Machine& victim = cluster.machines()[0];

    // Three kinds of nonsense from a raw connection, including a length field
    // large enough to exhaust memory if it were believed, and a client that
    // hangs up in the middle of a request.
    const std::vector<std::string> garbage = {
        "HELLO THERE\n",
        "ARGS 99999999999999\n",
        "ARGS 2\n5\nabc",  // a length that lies, then silence
    };
    for (const std::string& junk : garbage) {
        mp1::Conn raw = mp1::Connect(victim.host, victim.port,
                                     std::chrono::milliseconds(1000), &err);
        REQUIRE(raw.valid(), err);
        raw.WriteAll(junk, &err);
        raw = mp1::Conn();  // hang up
    }

    // The daemon must still be there, and still correct.
    const mp1::QuerySummary summary =
        mp1::RunQuery(cluster.machines(), {mp1::kSomewhatToken}, TestOptions());
    CheckEveryMachine(cluster, summary, mp1::kSomewhatToken);
    CHECK_EQ(summary.machines_failed, 0);
}

// --- concurrency and querier-independence ---------------------------------

TEST(Distributed_AnyMachineCanBeTheQuerier) {
    mp1test::Cluster cluster;
    std::string err;
    REQUIRE(cluster.Start(5, NextBasePort(), &err), err);

    // "Any machine can query" is a property of the client holding no state
    // about where it runs: machine 1 is not privileged, and the order the
    // machines appear in the config decides nothing. Rotating the list is the
    // in-process equivalent of running log-query from a different machine, and
    // every rotation has to produce the same answers, machine for machine.
    const std::vector<mp1::Machine> machines = cluster.machines();
    for (size_t start = 0; start < machines.size(); ++start) {
        const auto split = machines.begin() + static_cast<std::ptrdiff_t>(start);
        std::vector<mp1::Machine> rotated(split, machines.end());
        rotated.insert(rotated.end(), machines.begin(), split);

        const mp1::QuerySummary summary =
            mp1::RunQuery(rotated, {mp1::kSomeLogsToken}, TestOptions());

        CHECK_EQ(summary.total_lines, cluster.ExpectedTotal(mp1::kSomeLogsToken));
        CHECK_EQ(summary.machines_failed, 0);
        for (const mp1::Machine& machine : machines) {
            CHECK_EQ(LinesFrom(summary, machine.id),
                     cluster.ExpectedOn(mp1::kSomeLogsToken, machine.id));
        }
        // The table always follows the order it was given, whoever is asking.
        CHECK_EQ(summary.results.front().machine.id, rotated.front().id);
    }
}

TEST(Distributed_ConcurrentQueriesDoNotInterfere) {
    mp1test::Cluster cluster;
    std::string err;
    REQUIRE(cluster.Start(4, NextBasePort(), &err), err);

    // Four different questions asked at the same moment. Each has to come back
    // with its own answer -- shared mutable state in the daemon shows up here
    // as one query's results appearing in another's.
    const std::vector<std::string> tokens = {
        mp1::kRareToken, mp1::kSomewhatToken, mp1::kFrequentToken,
        mp1::kSomeLogsToken,
    };

    std::vector<std::future<mp1::QuerySummary>> queries;
    for (const std::string& token : tokens) {
        queries.push_back(std::async(std::launch::async, [&cluster, token] {
            return mp1::RunQuery(cluster.machines(), {token}, TestOptions());
        }));
    }

    for (size_t i = 0; i < tokens.size(); ++i) {
        const mp1::QuerySummary summary = queries[i].get();
        CHECK_EQ(summary.total_lines, cluster.ExpectedTotal(tokens[i]));
        CHECK_EQ(summary.machines_failed, 0);
    }
}

TEST(Distributed_RepeatedQueriesAreStable) {
    mp1test::Cluster cluster;
    std::string err;
    REQUIRE(cluster.Start(4, NextBasePort(), &err), err);

    // Cheap, and it is what surfaces the race that only shows up one run in
    // ten: a leftover byte in a connection buffer, a chunk boundary handled
    // wrongly under a different arrival pattern.
    const mp1::QuerySummary first =
        mp1::RunQuery(cluster.machines(), {mp1::kFrequentToken}, TestOptions());
    const std::set<std::string> first_lines = AllLines(first);
    CHECK(first.total_lines > 0);

    for (int attempt = 0; attempt < 20; ++attempt) {
        const mp1::QuerySummary again =
            mp1::RunQuery(cluster.machines(), {mp1::kFrequentToken}, TestOptions());
        if (again.total_lines != first.total_lines ||
            AllLines(again) != first_lines) {
            CHECK_EQ(again.total_lines, first.total_lines);
            std::printf("  run %d differed from the first\n", attempt);
            return;
        }
    }
    CHECK(true);  // twenty identical runs
}

// --- scale ----------------------------------------------------------------

TEST(Distributed_LargeLogFiles) {
    mp1test::Cluster cluster;
    std::string err;

    // The demo's size: ~300,000 lines per machine. Correctness only -- timing
    // belongs in scripts/measure.sh, where it can be run against the real VMs
    // and repeated properly.
    REQUIRE(cluster.Start(3, NextBasePort(), &err, 300000), err);

    for (const char* token : {mp1::kRareToken, mp1::kSomewhatToken}) {
        const mp1::QuerySummary summary =
            mp1::RunQuery(cluster.machines(), {token}, TestOptions());
        CheckEveryMachine(cluster, summary, token);
        CHECK_EQ(summary.machines_failed, 0);
    }
}

TEST(Distributed_EmptyLogFile) {
    mp1test::Cluster cluster;
    std::string err;
    REQUIRE(cluster.Start(3, NextBasePort(), &err), err);

    // A machine whose log exists but is empty -- a VM that rebooted and had its
    // logs regenerated but not yet filled. Zero matches, NO MATCH, no crash,
    // and the other machines completely unaffected.
    const std::string path = cluster.log_dir() + "/" + mp1::LogFileName(2);
    REQUIRE(::truncate(path.c_str(), 0) == 0, "could not truncate the log");

    const mp1::QuerySummary summary =
        mp1::RunQuery(cluster.machines(), {mp1::kFrequentToken}, TestOptions());

    CHECK_EQ(LinesFrom(summary, 2), uint64_t{0});
    CHECK_EQ(StatusOf(summary, 2), std::string("NO MATCH"));
    CHECK_EQ(summary.machines_ok, 3);
    CHECK_EQ(summary.machines_failed, 0);
    for (int id : {1, 3}) {
        CHECK_EQ(LinesFrom(summary, id), cluster.ExpectedOn(mp1::kFrequentToken, id));
    }
}
