// Local unit tests: the pieces that need no sockets.
//
// Get these green FIRST. Every one of these bugs is far cheaper to find here
// than as a mysterious hang across ten VMs.

#include "test_framework.hpp"

#include "mp1/config.hpp"
#include "mp1/grep_runner.hpp"
#include "mp1/log_gen.hpp"
#include "mp1/protocol.hpp"

// --- protocol -------------------------------------------------------------

TEST(EncodeDecodeRequest_RoundTrips) {
    // TODO: encode a Request, decode it, assert argv comes back identical.
    // Include awkward args: an empty string, a UTF-8 pattern, a 4 KB pattern,
    // one containing a literal newline, one that looks like a flag ("--color").
}

TEST(DecodeRequest_RejectsBadMagic) {
    // TODO: flip a magic byte -> must return false, not crash.
}

TEST(DecodeRequest_RejectsTruncatedBuffer) {
    // TODO: encode a valid request, then chop the buffer at every length from 0
    // to size-1. Every prefix must be rejected cleanly. This is the loop that
    // catches out-of-bounds reads in the decoder.
}

TEST(DecodeRequest_RejectsOversizedLengths) {
    // TODO: hand-build a buffer claiming argc = 60000 or len = 0xFFFFFFFF.
    // Must be rejected by the guardrails without attempting the allocation.
}

TEST(TrailerFrame_RoundTrips) {
    // TODO: including exit_code = 1 (no match) and a line_count above 2^32,
    // which catches a uint64 accidentally serialized as uint32.
}

// --- config ---------------------------------------------------------------

TEST(LoadMachines_ParsesValidFile) {
    // TODO: comments, blank lines, and extra whitespace are all tolerated.
}

TEST(LoadMachines_RejectsDuplicateIds) {
    // TODO: two machines with id 3 must fail loudly at load time.
}

TEST(LogFileName_MatchesSpec) {
    CHECK_EQ(mp1::LogFileName(1), std::string("machine.1.log"));
    CHECK_EQ(mp1::LogFileName(10), std::string("machine.10.log"));
}

// --- grep runner ----------------------------------------------------------

TEST(RunGrep_CountsLinesNotOccurrences) {
    // TODO: a file where one line contains the token twice -> line_count == 1.
}

TEST(RunGrep_HandlesMissingTrailingNewline) {
    // TODO: last line matches but has no '\n' -> still counted once.
}

TEST(RunGrep_NoMatchIsExitOneNotAnError) {
    // TODO: exit_code == 1, line_count == 0, and RunGrep itself returns true.
    // Conflating this with a failure is the single most common bug here: it
    // makes a legitimately empty answer look like a dead machine.
}

TEST(RunGrep_BadRegexReportsStderr) {
    // TODO: -E '(' -> exit_code == 2 and stderr_text is non-empty.
}

TEST(RunGrep_MissingLogFileReportsError) {
    // TODO: point at a nonexistent path -> exit 2, message reaches the caller.
}

TEST(RunGrep_LargeOutputDoesNotDeadlock) {
    // TODO: a pattern matching ~50 MB worth of lines. If you only drain stdout
    // and never stderr (or vice versa) this test hangs -- which is exactly the
    // point of having it. Give the suite a watchdog so a hang fails instead of
    // blocking CI forever.
}

TEST(RunGrep_PassesThroughGrepFlags) {
    // TODO: -i, -v, -w, -x, -n, -c, -F, and -E with alternation, character
    // classes, anchors and quantifiers. The spec calls out arbitrary -E regexes
    // specifically, so cover them properly.
}

TEST(RunGrep_DoesNotInvokeAShell) {
    // TODO: pattern "$(touch /tmp/mp1_pwned)" must match nothing and must not
    // create the file. Proves you used execvp and not system().
}

// --- log generator --------------------------------------------------------

TEST(GenerateLog_IsDeterministic) {
    // TODO: same seed + id twice -> byte-identical files.
}

TEST(GenerateLog_FillerNeverContainsPlantedTokens) {
    // TODO: generate with zero planted patterns, then grep for every token in
    // the vocabulary and assert zero hits. If this fails, every expected count
    // in the distributed suite is quietly wrong.
}

TEST(GenerateLog_ExpectedCountsMatchRealGrep) {
    // TODO: generate a file, then run the real grep over it locally and assert
    // the counts equal what ExpectedCounts predicted. This validates the oracle
    // itself -- without it, the distributed tests are only checking that two
    // pieces of your own code agree with each other.
}

TEST(GenerateLog_HitsTargetSize) {
    // TODO: --bytes 60000000 lands within a small tolerance of 60 MB.
}
