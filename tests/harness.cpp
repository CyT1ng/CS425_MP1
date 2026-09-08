#include "harness.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "mp1/net.hpp"

namespace mp1test {
namespace {

// How long a daemon gets to come up before the cluster is declared broken.
// Generous, because it is only ever waited out when something is genuinely
// wrong -- the happy path takes a few milliseconds.
constexpr auto kStartupBudget = std::chrono::seconds(10);
constexpr auto kPollInterval  = std::chrono::milliseconds(20);

// The daemon under test. The tests are run from the repo root (`make test`),
// and the environment variable is there for when they are not.
std::string ServerBinary() {
    const char* from_env = ::getenv("MP1_SERVER_BIN");
    return from_env != nullptr ? from_env : "./bin/log-server";
}

// Starts one daemon. Returns its pid, or -1 with a reason in `err`.
pid_t StartDaemon(int id, uint16_t port, const std::string& log_dir,
                  std::string* err) {
    const std::string binary    = ServerBinary();
    const std::string id_text   = std::to_string(id);
    const std::string port_text = std::to_string(port);
    const std::string out_path  = log_dir + "/server." + id_text + ".out";

    const pid_t pid = ::fork();
    if (pid < 0) {
        *err = std::string("fork: ") + std::strerror(errno);
        return -1;
    }
    if (pid == 0) {
        // The daemon's banner, and anything grep complains about, goes to a
        // file: the test output stays readable, and a failing test still has
        // somewhere to look.
        const int out = ::open(out_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out >= 0) {
            ::dup2(out, STDOUT_FILENO);
            ::dup2(out, STDERR_FILENO);
            ::close(out);
        }
        ::execl(binary.c_str(), "log-server", "--id", id_text.c_str(),
                "--port", port_text.c_str(), "--log-dir", log_dir.c_str(),
                static_cast<char*>(nullptr));
        ::_exit(127);  // only reached if the binary is missing
    }
    return pid;
}

// Waits until `machine` accepts a connection. Sets *pid to -1 if the daemon
// turns out to have died, so nobody later signals a pid the OS has recycled.
bool WaitUntilAccepting(const mp1::Machine& machine, pid_t* pid,
                        const std::string& log_dir, std::string* err) {
    const auto deadline = std::chrono::steady_clock::now() + kStartupBudget;
    while (std::chrono::steady_clock::now() < deadline) {
        std::string ignored;
        mp1::Conn probe = mp1::Connect(machine.host, machine.port,
                                       std::chrono::milliseconds(200), &ignored);
        if (probe.valid()) return true;

        // If the daemon has already exited, there is nothing to wait for. Fail
        // now with somewhere to look rather than after the whole budget.
        int status = 0;
        if (::waitpid(*pid, &status, WNOHANG) == *pid) {
            *pid = -1;
            *err = "log-server for machine " + std::to_string(machine.id) +
                   " exited before accepting connections; see " + log_dir +
                   "/server." + std::to_string(machine.id) + ".out";
            return false;
        }
        std::this_thread::sleep_for(kPollInterval);
    }
    *err = "machine " + std::to_string(machine.id) + " did not start listening on port " +
           std::to_string(machine.port) + " in time";
    return false;
}

}  // namespace

bool Cluster::Start(int n, uint16_t base_port, std::string* err,
                    uint64_t lines_per_machine) {
    StopAll();  // reusing a Cluster starts from a clean slate

    char dir_template[] = "/tmp/mp1_cluster_XXXXXX";
    if (::mkdtemp(dir_template) == nullptr) {
        *err = std::string("mkdtemp: ") + std::strerror(errno);
        return false;
    }
    log_dir_ = dir_template;

    // The machine list is filled in first, before anything can fail, so that
    // StopAll knows which files to remove however this ends.
    for (int id = 1; id <= n; ++id) {
        machines_.push_back(
            mp1::Machine{id, "127.0.0.1", static_cast<uint16_t>(base_port + id)});
    }

    for (const mp1::Machine& machine : machines_) {
        mp1::LogSpec spec;
        spec.machine_id = machine.id;
        spec.line_count = lines_per_machine;
        spec.seed       = kSeed;
        spec.patterns   = mp1::DefaultPatterns();
        if (!mp1::GenerateLog(spec, log_dir_ + "/" + mp1::LogFileName(machine.id),
                              expected_, err)) {
            return false;
        }
    }

    for (const mp1::Machine& machine : machines_) {
        const pid_t pid = StartDaemon(machine.id, machine.port, log_dir_, err);
        if (pid < 0) return false;
        pids_.push_back(pid);
    }

    // Every daemon is launched before any of them is waited for, so starting a
    // ten-machine cluster costs one startup, not ten.
    for (size_t i = 0; i < pids_.size(); ++i) {
        if (!WaitUntilAccepting(machines_[i], &pids_[i], log_dir_, err)) {
            return false;
        }
    }
    return true;
}

bool Cluster::Kill(int machine_id, std::string* err) {
    for (size_t i = 0; i < machines_.size(); ++i) {
        if (machines_[i].id != machine_id) continue;
        if (pids_[i] < 0) return true;  // already dead; killing twice is not an error

        if (::kill(pids_[i], SIGKILL) != 0 && errno != ESRCH) {
            *err = "kill machine " + std::to_string(machine_id) + ": " +
                   std::strerror(errno);
            return false;
        }
        // Reaped immediately: an unreaped zombie would still hold its pid, and
        // a later StopAll would signal it again.
        int status = 0;
        while (::waitpid(pids_[i], &status, 0) < 0 && errno == EINTR) {
        }
        pids_[i] = -1;
        return true;
    }
    *err = "no machine with id " + std::to_string(machine_id);
    return false;
}

void Cluster::StopAll() {
    for (int& pid : pids_) {
        if (pid < 0) continue;
        ::kill(pid, SIGKILL);
        int status = 0;
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
        pid = -1;
    }
    pids_.clear();

    // Only the files this harness created, then the directory itself. No
    // shelling out to `rm -rf`: a bug in the path would run that as you.
    if (!log_dir_.empty()) {
        for (const mp1::Machine& machine : machines_) {
            const std::string id_text = std::to_string(machine.id);
            ::unlink((log_dir_ + "/" + mp1::LogFileName(machine.id)).c_str());
            ::unlink((log_dir_ + "/server." + id_text + ".out").c_str());
        }
        ::rmdir(log_dir_.c_str());
        log_dir_.clear();
    }

    machines_.clear();
    expected_ = mp1::ExpectedCounts{};
}

uint64_t Cluster::ExpectedOn(const std::string& token, int machine_id) const {
    const auto by_machine = expected_.by_token.find(token);
    if (by_machine == expected_.by_token.end()) return 0;
    const auto entry = by_machine->second.find(machine_id);
    return entry == by_machine->second.end() ? 0 : entry->second;
}

uint64_t Cluster::ExpectedTotal(const std::string& token) const {
    return expected_.TotalFor(token);
}

}  // namespace mp1test
