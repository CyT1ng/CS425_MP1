// log-server -- serves grep queries against this machine's log.
// One instance runs on every VM.
//
//   log-server --id <machine-id> [--port 4425] [--log-dir .]
//
// Accepts a connection, reads one Request, greps this machine's own
// machine.<id>.log, streams the result back, closes. One connection per query.
//
// Concurrency: handle each connection on its own thread. Even though a single
// querier issues one request per machine, the unit tests fan several queries in
// at once and the demo may have both partners querying simultaneously. A
// single-threaded accept loop would serialize them and look like a hang.
//
// Robustness: a server that dies is indistinguishable from a failed VM, which
// is fine for fail-stop -- but do not let a MALFORMED request kill the daemon.
// Catch per-connection errors, reply with an ERROR frame, and keep listening.

#include <cstdio>
#include <string>

#include "mp1/config.hpp"
#include "mp1/grep_runner.hpp"
#include "mp1/net.hpp"
#include "mp1/protocol.hpp"

namespace {

void Usage() {
    std::fprintf(stderr,
                 "usage: log-server --id <machine-id> [--port <port>] "
                 "[--log-dir <dir>]\n");
}

}  // namespace

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    Usage();
    // TODO:
    //   1. Parse args; --id is required (it selects this machine's log file).
    //   2. Ignore SIGPIPE, or a querier that disconnects mid-stream kills you.
    //   3. fd = mp1::Listen(port)
    //   4. accept loop -> spawn a detached thread per connection:
    //        - ReadFull the request, DecodeRequest
    //        - RunGrep(args, log_dir + "/" + LogFileName(id), sink, ...)
    //          where `sink` wraps each chunk in a DATA frame and WriteFulls it
    //        - send TRAILER with grep's exit code and the counted lines
    //        - on any failure before/instead of that, send ERROR
    //        - close
    return 1;
}
