// server.cpp -- one machine's log daemon, complete, in one file.
//
//   ./server <machine-id> <port> <log-dir>
//
// It waits for a connection, reads a grep command, runs the REAL grep on this
// machine's own log file, sends the matching lines back, and closes. That is
// the entire job.
//
//   g++ -std=c++17 -pthread server.cpp -o server
//
// ---------------------------------------------------------------------------
// THE WIRE FORMAT, in full. It is plain text so you can read it with your eyes.
//
//   client -> server     one line per grep argument, then an empty line:
//                            -c
//                            ERROR
//                            <empty line>
//
//   server -> client     D <n>\n  followed by exactly n bytes   (0 or more)
//                        E <grep's exit code>\n                 (exactly once)
//
// Two questions you should be able to answer about this:
//
// 1. Why "D <n>" instead of just sending the lines? Because a log line could
//    itself begin with "E 0", and then the client could not tell a result from
//    the end marker. The length says exactly where the chunk stops.
//
// 2. Why send an E line at all? Because without it, "grep found nothing" and
//    "this machine just died" look identical on the network -- both are a
//    closed connection with nothing in it. The E line is the difference
//    between an answer and a silence.
// ---------------------------------------------------------------------------

#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

// Runs grep on `log_file`, writing D-frames to `out` as output appears.
// Returns grep's exit code: 0 = matched, 1 = matched nothing, 2 = grep failed.
//
// Note that 1 is NOT an error. "No lines matched" is a correct answer, and
// confusing it with failure is the most common bug in this whole project.
int run_grep(const std::vector<std::string>& args, const std::string& log_file,
             FILE* out) {
    int pipe_fd[2];
    if (pipe(pipe_fd) != 0) return 2;

    pid_t pid = fork();
    if (pid < 0) return 2;

    if (pid == 0) {
        // ---- child: turn into grep ----
        close(pipe_fd[0]);
        dup2(pipe_fd[1], STDOUT_FILENO);  // grep's stdout goes down the pipe
        close(pipe_fd[1]);

        // argv as an ARRAY, handed straight to execvp. Never system() or
        // popen(): those run a shell, which would re-interpret the pattern, so
        // -E '(a|b)+' would break depending on quoting -- and it would hand a
        // stranger on the network a shell on this machine.
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>("grep"));
        argv.push_back(const_cast<char*>("-H"));  // print the filename on each line
        for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(const_cast<char*>(log_file.c_str()));
        argv.push_back(nullptr);

        execvp("grep", argv.data());
        _exit(2);  // only reached if grep could not be started at all
    }

    // ---- parent ----
    // Close the write end FIRST. If we keep our copy open, the pipe never
    // reports end-of-file and the loop below waits forever.
    close(pipe_fd[1]);

    char buf[4096];
    ssize_t n;
    while ((n = read(pipe_fd[0], buf, sizeof(buf))) > 0) {
        // Read and forward as we go. Do NOT wait for grep to finish first: a
        // pipe only holds about 64 KB, so on a big result grep would block
        // writing while we block waiting, and neither would ever move again.
        fprintf(out, "D %zd\n", n);
        fwrite(buf, 1, n, out);
    }
    close(pipe_fd[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 2;
}

// Handles one client, then hangs up. Runs on its own thread so that several
// people can query this machine at the same time.
void handle_client(int fd, int machine_id) {
    // Two FILE* on one socket: one to read with, one to write with. fdopen
    // gives us fgets and fwrite, which saves writing buffering code by hand.
    FILE* in  = fdopen(fd, "r");
    FILE* out = fdopen(dup(fd), "w");
    if (!in || !out) return;

    // Read the grep arguments: one per line, until a blank line.
    std::vector<std::string> args;
    char line[1024];
    while (fgets(line, sizeof(line), in)) {
        std::string s(line);
        if (!s.empty() && s.back() == '\n') s.pop_back();
        if (s.empty()) break;  // blank line = end of the request
        args.push_back(s);
    }

    // THE FILENAME IS ADDED HERE, by us, from our own id -- it never comes off
    // the network. So machine 3 always searches machine.3.log, and a client
    // cannot ask us to search something else.
    std::string log_file = "machine." + std::to_string(machine_id) + ".log";

    int code = run_grep(args, log_file, out);
    fprintf(out, "E %d\n", code);
    fflush(out);

    fclose(in);
    fclose(out);
}

int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s <machine-id> <port> <log-dir>\n", argv[0]);
        return 1;
    }
    int machine_id = atoi(argv[1]);
    int port       = atoi(argv[2]);
    const char* log_dir = argv[3];

    // Writing to a client that already hung up would kill this process by
    // default. One dead client must not take the server with it.
    signal(SIGPIPE, SIG_IGN);

    // Move into the log directory so that grep -H prints "machine.3.log:"
    // rather than the whole path. The output has to name the file it came from.
    if (chdir(log_dir) != 0) {
        perror("chdir");
        return 1;
    }

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int on = 1;
    // Without SO_REUSEADDR, restarting the server fails for a minute or so
    // because the old socket is still in TIME_WAIT. You will restart it a lot.
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);  // accept from any machine
    addr.sin_port        = htons(port);

    if (bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) != 0) {
        perror("bind");
        return 1;
    }
    listen(listen_fd, 16);
    fprintf(stderr, "machine %d serving machine.%d.log on port %d\n",
            machine_id, machine_id, port);

    for (;;) {
        int fd = accept(listen_fd, nullptr, nullptr);
        if (fd < 0) continue;
        // A thread per client, detached: this loop goes straight back to
        // accepting, so a slow query does not block the next person.
        std::thread(handle_client, fd, machine_id).detach();
    }
}
