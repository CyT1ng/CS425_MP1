// query.cpp -- the querier, complete, in one file.
//
//   ./query <machines.txt> <grep args...>
//   ./query machines.txt -c ERROR
//
// It asks EVERY machine the same question AT THE SAME TIME, waits for the
// answers, and prints them. Machines that are down are reported, not skipped.
//
//   g++ -std=c++17 -pthread query.cpp -o query
//
// The one idea that matters in this file:
//
//   Ask all N machines at once  -> the query takes as long as the SLOWEST one.
//   Ask them one after another  -> it takes the SUM of all of them.
//
// With 10 machines that is the difference between 40 ms and 400 ms, and it is
// also what stops one dead machine from holding up the other nine.

#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <future>
#include <sstream>
#include <string>
#include <vector>

struct Machine {
    int         id;
    std::string host;
    int         port;
};

struct Result {
    Machine     machine;
    std::string output;      // the matching lines this machine sent back
    bool        answered = false;   // did we get the E line?
    int         exit_code = 2;
    std::string error;       // why not, when it did not answer
    long        ms = 0;      // how long this one machine took
};

// Reads "<id> <host> <port>" lines, skipping blanks and # comments.
std::vector<Machine> load_machines(const std::string& path) {
    std::vector<Machine> machines;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream fields(line);
        Machine m;
        if (fields >> m.id >> m.host >> m.port) machines.push_back(m);
    }
    return machines;
}

// Connects, but gives up after timeout_ms. Returns -1 if we could not get in.
//
// The timeout is the whole point. If a machine is switched off, nothing answers
// at all, and a plain connect() will sit there for MINUTES before giving up --
// so one dead machine would stall the entire query. A socket timeout does not
// help here (the kernel ignores it for connect), so the trick is: make the
// socket non-blocking, start the connection, and wait on it ourselves.
int connect_with_timeout(const std::string& host, int port, int timeout_ms) {
    addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* found = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found) != 0) {
        return -1;  // the name does not resolve
    }

    int fd = socket(found->ai_family, found->ai_socktype, 0);
    fcntl(fd, F_SETFL, O_NONBLOCK);          // "start it, do not wait"
    connect(fd, found->ai_addr, found->ai_addrlen);
    freeaddrinfo(found);

    // Wait for the socket to become writable, which means the attempt finished.
    pollfd waiting{fd, POLLOUT, 0};
    if (poll(&waiting, 1, timeout_ms) != 1) {
        close(fd);
        return -1;  // nobody answered in time: the machine is gone
    }

    // Finished, but did it succeed? SO_ERROR holds the verdict.
    int err = 0;
    socklen_t len = sizeof(err);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
    if (err != 0) {
        close(fd);
        return -1;  // usually "connection refused": machine up, server not running
    }

    fcntl(fd, F_SETFL, 0);  // back to ordinary blocking mode
    return fd;
}

// Everything one machine involves: connect, ask, listen, hang up.
//
// This function NEVER fails upwards. A dead machine is a normal Result with
// `answered = false`, not an error that stops the whole query. That single
// choice is what makes the system fault tolerant.
Result query_one(Machine machine, std::vector<std::string> args) {
    Result r;
    r.machine = machine;
    auto started = std::chrono::steady_clock::now();

    int fd = connect_with_timeout(machine.host, machine.port, 2000);
    if (fd < 0) {
        r.error = "unreachable";
    } else {
        FILE* out = fdopen(fd, "w");
        FILE* in  = fdopen(dup(fd), "r");

        // Send the request: one argument per line, then a blank line.
        for (const std::string& a : args) fprintf(out, "%s\n", a.c_str());
        fprintf(out, "\n");
        fflush(out);

        // Read frames until the E line.
        char header[64];
        while (fgets(header, sizeof(header), in)) {
            if (header[0] == 'D') {
                long n = atol(header + 2);
                std::string chunk(n, '\0');
                if ((long)fread(&chunk[0], 1, n, in) != n) {
                    r.error = "died halfway through answering";
                    break;
                }
                r.output += chunk;
            } else if (header[0] == 'E') {
                r.exit_code = atoi(header + 2);
                r.answered  = true;   // <- the machine finished its sentence
                break;
            } else {
                r.error = "unexpected reply (is that really our server?)";
                break;
            }
        }
        if (!r.answered && r.error.empty()) r.error = "died halfway through answering";

        fclose(in);
        fclose(out);
    }

    r.ms = std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - started).count();
    return r;
}

long count_lines(const std::string& text) {
    if (text.empty()) return 0;
    long lines = 0;
    for (char c : text) if (c == '\n') ++lines;
    if (text.back() != '\n') ++lines;   // a last line without a newline is still a line
    return lines;
}

int main(int argc, char** argv) {
    signal(SIGPIPE, SIG_IGN);
    if (argc < 3) {
        fprintf(stderr, "usage: %s <machines.txt> <grep args...>\n", argv[0]);
        return 2;
    }

    std::vector<Machine> machines = load_machines(argv[1]);
    if (machines.empty()) {
        fprintf(stderr, "no machines in %s\n", argv[1]);
        return 2;
    }
    std::vector<std::string> args(argv + 2, argv + argc);

    auto started = std::chrono::steady_clock::now();

    // ---- the important part ----
    // Start every machine BEFORE waiting for any of them. std::launch::async
    // is not optional: without it the work may be postponed until .get(), which
    // would quietly turn this back into asking them one at a time.
    std::vector<std::future<Result>> pending;
    for (const Machine& m : machines) {
        pending.push_back(std::async(std::launch::async, query_one, m, args));
    }

    std::vector<Result> results;
    for (std::future<Result>& task : pending) results.push_back(task.get());

    long total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - started).count();

    // The matching lines, machine by machine. Each line already says which log
    // it came from, because the server passed grep -H.
    for (const Result& r : results) fwrite(r.output.data(), 1, r.output.size(), stdout);

    // The summary. A machine that failed gets a WORD where its count should be:
    // printing 0, or leaving the row out, would look exactly like a machine
    // that was fine and simply had no matches.
    long total_lines = 0;
    int  ok = 0;
    for (const Result& r : results) {
        printf("machine.%d.log  ", r.machine.id);
        if (r.answered) {
            long n = count_lines(r.output);
            printf("%8ld lines   (%ld ms)\n", n, r.ms);
            total_lines += n;
            ++ok;
        } else {
            printf("%8s %s   (%ld ms)\n", "--", r.error.c_str(), r.ms);
        }
    }
    printf("---------------------------------------------\n");
    printf("total          %8ld lines from %d/%zu machines   (%ld ms)\n",
           total_lines, ok, machines.size(), total_ms);

    // Exit code, the same way grep does it, so this composes in a pipeline.
    if (ok < (int)machines.size()) return 2;   // somebody failed
    return total_lines > 0 ? 0 : 1;
}
