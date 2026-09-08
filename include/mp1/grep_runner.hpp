#pragma once
//
// Runs the real system grep on this machine's log file.
//
// The spec forbids reimplementing grep, and that is a gift: shelling out to the
// actual executable gives you every one of grep's options, including arbitrary
// -E regexes, for free and correctly.
//
// Three decisions to be able to defend at the demo:
//
//  * fork + execvp with an argv ARRAY. Never system() or "sh -c". A shell would
//    re-interpret the pattern, so -E '(foo|bar)+' would break depending on how
//    the client happened to quote it -- and it would hand a remote caller a
//    shell on your VM.
//
//  * The filename is appended HERE, by the server, from its own config. It
//    never comes off the wire.
//
//  * "-H" is prepended so every line carries its filename, which the spec
//    requires. GNU grep lets a later flag win, so a user passing -h or -c still
//    gets what they asked for.
//
// Simplification while learning: only grep's STDOUT is piped. Its stderr is
// inherited, so a bad regex writes to the daemon's own log where you can read
// it, and the client learns only that grep exited 2. Piping both would mean
// poll()ing two fds, because draining one while grep fills the other deadlocks
// -- real, but not what this MP is teaching. Add the second pipe when you add
// the protocol's error text.
//
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mp1 {

struct GrepResult {
    int      exit_code  = -1;   // 0 = matched, 1 = no match, 2 = error
    uint64_t line_count = 0;
};

// Called with each chunk of grep's stdout as it is produced. Return false to
// abort early -- used when the client has hung up and nobody wants the rest.
using ChunkSink = std::function<bool(const std::string& chunk)>;

// Runs grep on `log_path`, streaming its stdout through `sink` and counting
// lines as they go by. pipe() + fork() + execvp(), in that order:
//
//   - the parent closes the WRITE end of the pipe first, or it never sees EOF,
//     because one copy of that end is still open -- in itself
//   - it reads the pipe until read() returns 0, counting '\n' per chunk
//   - it waitpid()s LAST and records the real exit status
//
// Draining before reaping is not a style preference: a pipe holds ~64 KB, so
// waiting on the child first deadlocks on any larger result.
//
// Returns false only when grep could not be RUN. grep running and rejecting
// something -- a bad regex, a missing file -- is a successful call whose
// result->exit_code is 2, because "this machine is broken" and "your pattern is
// broken" are different answers.
//
// Edge cases with a test each in tests/test_unit_local.cpp:
//   - a final line with no trailing '\n' still counts as one line
//   - exit 1 with no output is SUCCESS (no match), not a failure
//   - exit 2 from a bad -E regex
//   - the log file missing on this machine
bool RunGrep(const std::vector<std::string>& user_args,
             const std::string& log_path,
             const ChunkSink& sink,
             GrepResult* result,
             std::string* err);

}  // namespace mp1
