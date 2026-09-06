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
//
// These run over a socketpair(), so they exercise the real Conn read/write
// loops and the real wire format without any network, ports, or daemons.

TEST(Conn_ReadLineHandlesSplitAndCoalescedReads) {
    // TODO: write "AB", then "C\nDEF\n", as two separate writes. ReadLine must
    // return "ABC" and then "DEF". One read() can stop in the middle of a line
    // or deliver two at once -- this is the test for the buffer inside Conn,
    // and it fails immediately if you forget to keep leftover bytes.
}

TEST(Conn_ReadExactlyDetectsShortStream) {
    // TODO: write 10 bytes, close the writing end, then ask for 100. Must fail,
    // not return the 10 as if that were all there was. A peer dying mid-message
    // is exactly this.
}

TEST(Protocol_RequestRoundTrips) {
    // TODO: SendRequest on one end, RecvRequest on the other; argv must come
    // back identical. Include awkward arguments: an empty string, a UTF-8
    // pattern, a 4 KB pattern, one containing a literal newline, and one that
    // looks like a flag ("--color"). The newline case is the entire reason
    // arguments are length-prefixed instead of one-per-line.
}

TEST(Protocol_DataAndEndFramesRoundTrip) {
    // TODO: SendData twice, then SendEnd; RecvFrame three times. Use
    // exit_code = 1 (no match) and a line_count above 2^32 -- that catches a
    // count that got parsed into a 32-bit type somewhere.
}

TEST(Protocol_RejectsGarbageHeader) {
    // TODO: write "HELLO\n" and assert RecvFrame fails cleanly rather than
    // hanging or crashing. This is what pointing dgrep at the wrong port looks
    // like, and it should say so instead of printing nonsense.
}

TEST(Protocol_TruncatedFrameIsDetected) {
    // TODO: send "D 100\n" but only 10 bytes of payload, then close. RecvFrame
    // must fail instead of handing back a short chunk as though it were whole.
    // This is the killed-mid-stream case that becomes kPartial.
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

TEST(RunGrep_BadRegexIsExitTwo) {
    // TODO: -E '(' -> exit_code == 2, and RunGrep itself still returns true:
    // it ran grep successfully, grep just disliked the pattern.
    // grep's message goes to the daemon's own stderr for now; carrying the text
    // back to the querier arrives with the protocol's error frame.
}

TEST(RunGrep_MissingLogFileIsExitTwo) {
    // TODO: point at a nonexistent path -> exit_code == 2. On the VMs this is
    // the "someone rebooted and the logs were not regenerated" case, so it
    // needs to look like an error and not like an empty result.
}

TEST(RunGrep_LargeOutputStreamsWithoutStalling) {
    // TODO: a pattern matching ~50 MB worth of lines. A pipe holds only ~64 KB,
    // so grep blocks on write() until you read; if the parent waits for the
    // child before draining, this deadlocks forever. Read first, waitpid last.
    // Give the suite a watchdog so a hang fails instead of hanging the run.
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
