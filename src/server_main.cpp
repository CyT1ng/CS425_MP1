// log-server -- serves grep queries against this machine's log.
// One instance runs on every VM.
//
//   log-server --id <machine-id> [--port 4425] [--log-dir .]
//
// Accepts a connection, reads one Request, greps this machine's own
// machine.<id>.log, streams the result back, closes. One connection per query.
//
// Concurrency: each connection is handled on its own thread. Even though a
// single querier issues one request per machine, the unit tests fan several
// queries in at once and the demo may have both partners querying
// simultaneously. A single-threaded accept loop would serialize them and look
// like a hang.
//
// Robustness: a server that dies is indistinguishable from a failed VM, which
// is fine for fail-stop -- but a MALFORMED request must never kill the daemon.
// Every per-connection failure ends that connection only, and the accept loop
// keeps running.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <system_error>
#include <thread>

#include "mp1/config.hpp"
#include "mp1/grep_runner.hpp"
#include "mp1/net.hpp"
#include "mp1/protocol.hpp"

namespace {

constexpr uint16_t kDefaultPort = 4425;

// A client that connects and then says nothing must not pin a thread forever.
constexpr std::chrono::milliseconds kRequestTimeout{30000};

// How long to pause after a failed accept(), so that a persistent failure --
// out of file descriptors, say -- reports itself once a second instead of
// spinning a core.
constexpr std::chrono::milliseconds kAcceptRetryPause{100};

void Usage() {
    std::fprintf(stderr,
                 "usage: log-server --id <machine-id> [--port <port>] "
                 "[--log-dir <dir>]\n");
}

// A numeric flag value, or a clear death. A typo'd --port has to fail at
// startup: silently becoming 0 would leave the daemon listening on a port
// nobody calls, which at the demo looks exactly like a dead machine.
long long RequireNumber(const char* flag, const char* value,
                        long long low, long long high) {
    const std::string text = value ? value : "";
    const bool numeric = !text.empty() && text.size() <= 18 &&
                         text.find_first_not_of("0123456789") == std::string::npos;
    const long long number = numeric ? std::stoll(text) : 0;
    if (!numeric || number < low || number > high) {
        std::fprintf(stderr, "log-server: %s expects a number in %lld..%lld, got \"%s\"\n",
                     flag, low, high, text.c_str());
        std::exit(2);
    }
    return number;
}

// One conversation, start to finish, on its own thread.
//
// Nothing in here may take the daemon down: the request came from a stranger,
// and the querier on the other end can vanish at any moment.
void ServeOneRequest(mp1::Conn& conn, const std::string& log_path) {
    std::string err;
    if (!conn.SetReadTimeout(kRequestTimeout, &err)) {
        std::fprintf(stderr, "log-server: %s\n", err.c_str());
        return;
    }

    mp1::Request request;
    if (!mp1::RecvRequest(conn, &request, &err)) {
        std::fprintf(stderr, "log-server: rejected a request: %s\n", err.c_str());
        // Answer anyway, with grep's "something went wrong" code. Closing in
        // silence is how a DEAD machine looks on the wire, and this one is
        // alive and simply did not understand the question.
        mp1::SendEnd(conn, 2, 0, &err);
        return;
    }

    // grep's output goes down the socket as it appears, one DATA frame per
    // chunk, so a 24 MB answer never has to fit in this daemon's memory.
    bool write_ok = true;
    const mp1::ChunkSink sink = [&](const std::string& chunk) {
        std::string write_err;
        write_ok = mp1::SendData(conn, chunk, &write_err);
        if (!write_ok) {
            std::fprintf(stderr, "log-server: querier hung up: %s\n",
                         write_err.c_str());
        }
        return write_ok;  // false stops grep rather than finishing into a closed socket
    };

    mp1::GrepResult grep;
    if (!mp1::RunGrep(request.argv, log_path, sink, &grep, &err)) {
        // grep could not be run at all. That is this machine's problem, not the
        // querier's pattern, so it is reported as grep's error code 2.
        std::fprintf(stderr, "log-server: %s\n", err.c_str());
        grep.exit_code = 2;
    }

    if (write_ok && !mp1::SendEnd(conn, grep.exit_code, grep.line_count, &err)) {
        std::fprintf(stderr, "log-server: could not send END: %s\n", err.c_str());
    }
}

// The thread body. An exception that escapes a thread calls std::terminate and
// takes the whole daemon down with it, so the last line of defence is here: one
// connection's problem stays one connection's problem.
void Serve(mp1::Conn conn, std::string log_path) {
    try {
        ServeOneRequest(conn, log_path);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "log-server: dropped a connection: %s\n", e.what());
    }
}

}  // namespace

int main(int argc, char** argv) {
    int         id      = 0;
    uint16_t    port    = kDefaultPort;
    std::string log_dir = ".";

    for (int i = 1; i < argc; ++i) {
        const std::string flag  = argv[i];
        const char*       value = (i + 1 < argc) ? argv[i + 1] : nullptr;

        if (flag == "--id") {
            id = static_cast<int>(RequireNumber("--id", value, 1, 100000));
            ++i;
        } else if (flag == "--port") {
            port = static_cast<uint16_t>(RequireNumber("--port", value, 1, 65535));
            ++i;
        } else if (flag == "--log-dir" && value) {
            log_dir = value;
            ++i;
        } else {
            std::fprintf(stderr, "log-server: unrecognised argument: %s\n",
                         flag.c_str());
            Usage();
            return 2;
        }
    }

    // --id is what selects this machine's log file, so there is no sensible
    // default for it: a daemon serving the wrong machine's log would answer
    // every query confidently and wrongly.
    if (id == 0) {
        std::fprintf(stderr, "log-server: --id is required\n");
        Usage();
        return 2;
    }

    // Before any socket exists: writing to a querier that just died would
    // otherwise kill this process outright, turning one failed machine into two.
    mp1::IgnoreSigpipe();

    const std::string log_path = log_dir + "/" + mp1::LogFileName(id);

    std::string err;
    const int listen_fd = mp1::Listen(port, &err);
    if (listen_fd < 0) {
        std::fprintf(stderr, "log-server: %s\n", err.c_str());
        return 1;
    }
    std::fprintf(stderr, "log-server: machine %d serving %s on port %u\n",
                 id, log_path.c_str(), static_cast<unsigned>(port));

    for (;;) {
        mp1::Conn conn = mp1::Accept(listen_fd, &err);
        if (!conn.valid()) {
            std::fprintf(stderr, "log-server: accept: %s\n", err.c_str());
            std::this_thread::sleep_for(kAcceptRetryPause);
            continue;
        }
        try {
            // Detached: this connection's thread owns everything it touches --
            // its socket, its own grep child -- so there is nothing to join and
            // no state shared with the accept loop.
            std::thread(Serve, std::move(conn), log_path).detach();
        } catch (const std::system_error& e) {
            // Out of threads. Drop this one connection and keep serving.
            std::fprintf(stderr, "log-server: cannot start a thread: %s\n",
                         e.what());
        }
    }
}
