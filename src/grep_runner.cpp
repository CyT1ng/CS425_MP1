#include "mp1/grep_runner.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace mp1 {
namespace {

// A pipe holds about 64 KB, so reading in the same unit keeps the child from
// blocking on write() any more often than it has to.
constexpr size_t kPipeChunk = 64 * 1024;

// The exit status the child uses when it never got as far as running grep.
// 127 is the shell's convention for "command not found", and grep itself never
// exits with it, so it stays unambiguous.
constexpr int kCouldNotExec = 127;

bool Fail(std::string* err, const char* what) {
    if (err) *err = std::string(what) + ": " + std::strerror(errno);
    return false;
}

// Splits "/home/me/mp1/machine.3.log" into "/home/me/mp1" and "machine.3.log".
// The directory is empty when the path has no '/' in it.
void SplitPath(const std::string& path, std::string* dir, std::string* file) {
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        dir->clear();
        *file = path;
    } else {
        *dir  = path.substr(0, slash);
        *file = path.substr(slash + 1);
    }
}

// Reads grep's stdout to EOF, counting lines and handing every chunk to `sink`.
//
// Returns false only on a read error. `sink` asking to stop is a normal end:
// the client hung up and nobody wants the rest of the output.
bool DrainPipe(int fd, const ChunkSink& sink, uint64_t* line_count,
               std::string* err) {
    std::string chunk;
    uint64_t lines = 0;
    char last_byte = '\n';  // an empty result is zero lines, not one

    for (;;) {
        char buf[kPipeChunk];
        const ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) continue;
            *line_count = lines;
            return Fail(err, "read from grep");
        }
        if (n == 0) break;  // grep closed its stdout: the output is complete

        // Counted here, while the bytes are already in cache. Running grep a
        // second time with -c to get the number would re-read the whole 60 MB
        // file and roughly double the query's latency.
        lines += static_cast<uint64_t>(std::count(buf, buf + n, '\n'));
        last_byte = buf[n - 1];

        chunk.assign(buf, static_cast<size_t>(n));
        if (!sink(chunk)) break;  // nobody is listening any more
    }

    // grep's last line has no trailing newline when the log file's last line
    // had none. It is still a line, and grep would still count it as one.
    if (last_byte != '\n') ++lines;

    *line_count = lines;
    return true;
}

}  // namespace

// Runs the real grep on `log_path` and streams its stdout through `sink`.
//
// Returns false only when grep could not be RUN. grep running and reporting a
// problem -- a bad regex, a missing file -- is a successful call whose
// result->exit_code happens to be 2. The distinction matters: the first is a
// broken machine, the second is an answer.
bool RunGrep(const std::vector<std::string>& user_args,
             const std::string& log_path,
             const ChunkSink& sink,
             GrepResult* result,
             std::string* err) {
    std::string log_dir, log_file;
    SplitPath(log_path, &log_dir, &log_file);

    // argv for execvp, as an ARRAY -- never a command string. No shell ever
    // sees the pattern, so -E '(foo|bar)+' arrives at grep exactly as the user
    // typed it, and a remote caller cannot smuggle a command into it.
    //
    // "-H" leads so every line carries its filename, which the spec requires;
    // it comes first so that a user who passes -h or -c still wins.
    //
    // The filename is appended HERE, from the server's own config. It never
    // comes off the wire. (The pass-through args are still grep's, so a caller
    // could in principle name a second file of their own; the guarantee is that
    // this machine always searches its own log, not that grep is sandboxed.)
    std::vector<std::string> args;
    args.reserve(user_args.size() + 3);
    args.push_back("grep");
    args.push_back("-H");
    args.insert(args.end(), user_args.begin(), user_args.end());
    args.push_back(log_file);

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (std::string& arg : args) argv.push_back(&arg[0]);
    argv.push_back(nullptr);

    int pipe_fd[2];
    if (::pipe(pipe_fd) < 0) return Fail(err, "pipe");

    const pid_t pid = ::fork();
    if (pid < 0) {
        const bool failed = Fail(err, "fork");
        ::close(pipe_fd[0]);
        ::close(pipe_fd[1]);
        return failed;
    }

    if (pid == 0) {
        // ---- child: becomes grep ----
        ::close(pipe_fd[0]);
        // grep echoes back the path it was given, and the spec wants the log's
        // FILENAME. Moving into the log directory here -- in the child only,
        // never in the daemon -- makes -H print "machine.3.log:" instead of
        // "/home/me/mp1/machine.3.log:".
        if (!log_dir.empty() && ::chdir(log_dir.c_str()) != 0) {
            std::fprintf(stderr, "log-server: cannot enter %s: %s\n",
                         log_dir.c_str(), std::strerror(errno));
            ::_exit(2);  // the same code grep uses for a file it cannot read
        }
        if (::dup2(pipe_fd[1], STDOUT_FILENO) < 0) ::_exit(kCouldNotExec);
        ::close(pipe_fd[1]);
        // stderr is left alone on purpose: grep's complaint about a bad regex
        // lands in the daemon's own log, where it can be read. Carrying that
        // text back to the querier needs a second pipe -- see grep_runner.hpp.
        ::execvp("grep", argv.data());
        ::_exit(kCouldNotExec);  // only reached if grep could not be started
    }

    // ---- parent ----
    // Close the write end FIRST. While this process still holds a copy of it,
    // the pipe can never report EOF, and the read loop below would hang after
    // grep exits.
    ::close(pipe_fd[1]);

    // Drain first, reap second. A pipe holds only ~64 KB: waiting for grep to
    // exit before reading would deadlock on any result larger than that -- grep
    // blocked on write, us blocked on waitpid, neither able to move.
    uint64_t line_count = 0;
    std::string drain_err;
    const bool drained = DrainPipe(pipe_fd[0], sink, &line_count, &drain_err);
    ::close(pipe_fd[0]);

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) continue;
        return Fail(err, "waitpid");
    }

    // Recorded even when the drain failed: the caller still has to put a number
    // in its END frame, and it has to be the number of lines actually sent.
    result->line_count = line_count;

    if (!drained) {
        if (err) *err = drain_err;
        return false;
    }
    if (WIFEXITED(status)) {
        const int code = WEXITSTATUS(status);
        if (code == kCouldNotExec) {
            if (err) *err = "could not run grep (is it on PATH?)";
            return false;
        }
        result->exit_code = code;   // 0 matched, 1 no match, 2 grep said no
        return true;
    }

    // Killed by a signal -- normally SIGPIPE, because `sink` asked us to stop
    // and closing the read end left grep writing into nothing. grep ran; it was
    // just cut short, so this is an error in grep's terms, not in ours.
    result->exit_code = 2;
    return true;
}

}  // namespace mp1
