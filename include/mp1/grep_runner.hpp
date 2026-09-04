#pragma once
//
// Runs the real system grep on the local log file and streams its stdout to a
// sink. The spec forbids reimplementing grep, so this is deliberately a thin
// wrapper around the actual executable -- that is what buys you all of grep's
// options, including arbitrary -E regexes, for free.
//
// Design decisions to be able to defend at the demo:
//
//  * exec grep DIRECTLY with an argv array (fork + execvp). Do NOT go through
//    system() or "sh -c". A shell would re-interpret the pattern, so
//    -E '(foo|bar)+' would break in ways that depend on the client's quoting,
//    and it would hand a remote caller a shell.
//
//  * The file argument is appended HERE, by the server, from its own config.
//    It never comes off the wire.
//
//  * "-H" is prepended so every output line is prefixed with the filename,
//    which is what the spec requires. GNU grep lets a later flag override an
//    earlier one, so a user who passes -h, -c, or -l still gets what they
//    asked for.
//
//  * Count matched lines ON THE FLY while streaming. Running grep a second time
//    with -c to get the count would double the work on a 60 MB file and roughly
//    double your reported latency.
//
//  * Read stdout and stderr from separate pipes. If you only drain one and grep
//    fills the other, grep blocks forever on write() and your query hangs. Use
//    poll() on both fds.
//
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mp1 {

struct GrepResult {
    int32_t  exit_code  = -1;  // 0 = matched, 1 = no match, 2 = error
    uint64_t line_count = 0;
    std::string stderr_text;   // captured so a bad regex reports a real reason
};

// Called with each chunk of grep stdout as it is produced. Returning false asks
// the runner to abort early (used when the client has hung up).
using ChunkSink = std::function<bool(const uint8_t* data, size_t len)>;

// TODO: implement with pipe(), fork(), execvp("grep", ...).
//
// Sketch of the parent loop:
//   - poll() on {stdout_fd, stderr_fd}
//   - on stdout data: count '\n' in the chunk, then hand it to `sink`
//   - on stderr data: append to result.stderr_text (cap it; a pathological
//     grep error should not let a peer balloon your memory)
//   - on both closed: waitpid() and record the real exit status
//
// Edge cases worth a test each:
//   - a final line with no trailing newline (still one matched line)
//   - grep exiting 1 with zero output (no match -- success, not failure)
//   - grep exiting 2 (bad -E regex) -- stderr must reach the querier
//   - the log file not existing on this machine
bool RunGrep(const std::vector<std::string>& user_args,
             const std::string& log_path,
             const ChunkSink& sink,
             GrepResult& result,
             std::string* err);

}  // namespace mp1
