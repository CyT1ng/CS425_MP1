#include "mp1/grep_runner.hpp"

namespace mp1 {

// See grep_runner.hpp. One pipe (stdout only), fork, execvp, drain, waitpid.

bool RunGrep(const std::vector<std::string>& user_args,
             const std::string& log_path,
             const ChunkSink& sink,
             GrepResult* result,
             std::string* err) {
    (void)user_args; (void)log_path; (void)sink; (void)result; (void)err;
    // TODO: build argv as { "grep", "-H", user_args..., log_path, nullptr },
    // pipe(), fork(), and in the child dup2 the write end onto stdout and
    // execvp. In the parent, close the write end FIRST -- otherwise the pipe
    // never reports EOF, because you are still holding it open yourself.
    return false;
}

}  // namespace mp1
