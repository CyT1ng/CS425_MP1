#include "mp1/grep_runner.hpp"

namespace mp1 {

// TODO: fork + execvp. See grep_runner.hpp for the full contract.
//
// Order of operations in the parent, roughly:
//   pipe(out_fds); pipe(err_fds);
//   pid = fork();
//   child:  dup2 both pipe write ends onto STDOUT/STDERR, close all other fds,
//           build argv = {"grep", "-H", <user args...>, log_path, nullptr},
//           execvp("grep", argv); _exit(127) if exec itself fails.
//   parent: close the write ends (forgetting this means you never see EOF and
//           the poll loop hangs forever), poll both read ends until both close,
//           then waitpid and pull the exit status out of WEXITSTATUS.

bool RunGrep(const std::vector<std::string>& user_args,
             const std::string& log_path,
             const ChunkSink& sink,
             GrepResult& result,
             std::string* err) {
    (void)user_args; (void)log_path; (void)sink; (void)result;
    if (err) *err = "RunGrep not implemented";
    return false;
}

}  // namespace mp1
