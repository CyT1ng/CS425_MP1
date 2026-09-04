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

#include "mp1/client.hpp"
#include "mp1/log_gen.hpp"

// --- the required frequency axis -----------------------------------------

TEST(Distributed_RarePattern) {
    // TODO: ~1e-5 of lines. Assert the total equals ExpectedCounts exactly, and
    // that the per-machine breakdown matches machine by machine -- a total that
    // happens to add up while individual attributions are wrong is a real bug
    // and a plain total-only assertion will not catch it.
}

TEST(Distributed_SomewhatFrequentPattern) {
    // TODO: ~1e-3 of lines.
}

TEST(Distributed_FrequentPattern) {
    // TODO: ~1e-1 of lines. This is also the large-transfer path: the result
    // set spans many DATA frames, so it is what actually exercises your
    // streaming reader and its buffer-boundary handling.
}

// --- the required distribution axis --------------------------------------

TEST(Distributed_PatternInExactlyOneLog) {
    // TODO: planted on machine 3 only. Every other machine must report 0 with
    // status kNoMatch -- NOT unreachable, NOT omitted from the output.
}

TEST(Distributed_PatternInSomeLogs) {
    // TODO: planted on a subset; assert the exact set of machines reporting
    // hits, not just the total.
}

TEST(Distributed_PatternInAllLogs) {
    // TODO: planted everywhere; total == sum of per-machine expectations.
}

TEST(Distributed_PatternInNoLog) {
    // TODO: a token never planted anywhere -> 0 everywhere, dgrep exit code 1,
    // and no machine marked failed.
}

// --- output correctness ---------------------------------------------------

TEST(Distributed_OutputLinesMatchLocalGroundTruth) {
    // TODO: the strongest assertion in the suite. Compare the full set of
    // returned lines against the lines a local grep produces over the same
    // generated files, as sets. Counts can coincidentally match while the
    // content is wrong; this catches that.
}

TEST(Distributed_EveryLineIsPrefixedWithItsFilename) {
    // TODO: every output line starts with "machine.<id>.log:" and the id always
    // matches the machine that actually sent it. This is a hard spec
    // requirement, so assert it rather than trusting -H.
}

TEST(Distributed_LinesAreNeverInterleavedMidLine) {
    // TODO: run a frequent query and assert every output line parses as
    // "machine.N.log:<rest>". Torn lines mean the stdout mutex is missing or
    // held at the wrong granularity -- and this only ever shows up under a
    // large, multi-machine result set.
}

// --- fault tolerance ------------------------------------------------------

TEST(FaultTolerance_QuerySucceedsWithOneMachineDown) {
    // TODO: Kill(2), then query. Live machines return complete, correct
    // results; machine 2 is reported kUnreachable. The spec's core requirement:
    // "it should fetch answers from all machines that have not failed."
}

TEST(FaultTolerance_QuerySucceedsWithMostMachinesDown) {
    // TODO: kill all but one. The survivor's answer must still be exact.
}

TEST(FaultTolerance_AllMachinesDown) {
    // TODO: no crash, no hang; every machine reported unreachable, exit 2.
}

TEST(FaultTolerance_DeadMachineDoesNotDelayLiveOnes) {
    // TODO: point one config entry at a blackhole address (a routable IP that
    // silently drops, not localhost -- localhost gives you an instant
    // ECONNREFUSED and tests nothing). Assert wall_latency is bounded by
    // connect_timeout and does not grow with the number of dead machines.
    // Fails loudly if the fan-out is accidentally serial.
}

TEST(FaultTolerance_MachineKilledMidStream) {
    // TODO: start a frequent query, kill a daemon while it is streaming.
    // That machine must be reported kPartial (bytes received, no trailer) --
    // not silently truncated into a plausible-looking wrong count. This is what
    // the trailer's line_count is for.
}

TEST(FaultTolerance_DaemonSurvivesMalformedRequest) {
    // TODO: connect raw, send garbage, disconnect. Then issue a normal query
    // and assert it still works. One bad client must not take down the daemon.
}

// --- concurrency and querier-independence ---------------------------------

TEST(Distributed_AnyMachineCanBeTheQuerier) {
    // TODO: issue the same query from the perspective of several different
    // machines and assert identical results. The spec requires that any machine
    // can query; a design that accidentally privileges machine 1 fails here.
}

TEST(Distributed_ConcurrentQueriesDoNotInterfere) {
    // TODO: several simultaneous queries with different patterns; each gets its
    // own correct answer. Catches shared mutable state in the daemon.
}

TEST(Distributed_RepeatedQueriesAreStable) {
    // TODO: the same query 20 times -> identical results every time. Cheap, and
    // it surfaces the race that only shows up one run in ten.
}

// --- scale ----------------------------------------------------------------

TEST(Distributed_LargeLogFiles) {
    // TODO: at least the demo's ~300,000 lines per machine, correctness only.
    // Timing belongs in scripts/measure.sh, not in the test suite.
}

TEST(Distributed_EmptyLogFile) {
    // TODO: a zero-byte machine.i.log -> 0 matches, kNoMatch, no crash.
}
