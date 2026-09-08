#pragma once
//
// Brings up a throwaway cluster so the distributed tests can run with zero
// manual intervention -- which the spec requires -- on a laptop as well as on
// the VMs.
//
// Local mode is what makes this practical: N log-server processes on
// 127.0.0.1, each on its own port with its own log directory. That is a real
// distributed system as far as the code under test is concerned (real sockets,
// real fan-out, real per-peer failures), and it means you are not blocked on VM
// provisioning and can iterate in seconds.
//
#include <cstdint>
#include <string>
#include <vector>

#include "mp1/config.hpp"
#include "mp1/log_gen.hpp"

namespace mp1test {

// Lines per machine unless a test asks for more. Large enough that the frequent
// pattern (10% of lines) spans many DATA frames and really does exercise the
// streaming reader, small enough that a test costs a fraction of a second.
inline constexpr uint64_t kDefaultLines = 20000;

// Every cluster is generated from this seed, so a failing assertion can be
// reproduced by hand:
//     ./bin/log-gen --id 3 --seed 424242 --lines 20000 --out-dir /tmp
inline constexpr uint64_t kSeed = 424242;

class Cluster {
public:
    // Generates logs for ids 1..n in a temp directory, starts one log-server
    // per machine on base_port + id, and returns once every one of them is
    // accepting connections.
    //
    // Readiness is POLLED, never slept for. A fixed sleep is either too short
    // (and the suite is flaky on a loaded VM) or too long (and the suite
    // crawls); connecting in a loop is neither.
    //
    // The logs are planted with mp1::DefaultPatterns(), so every token in
    // log_gen.hpp is available to a test and expected() knows the right answer
    // for each of them.
    bool Start(int n, uint16_t base_port, std::string* err,
               uint64_t lines_per_machine = kDefaultLines);

    // SIGKILLs one machine's daemon: fail-stop, the way a VM vanishes. Not
    // SIGTERM -- a clean shutdown closes its sockets politely, which is a
    // different code path from a machine that simply stops existing.
    bool Kill(int machine_id, std::string* err);

    // Kills every daemon and removes the temp directory. Also runs from the
    // destructor, so it happens even when a test fails partway -- otherwise the
    // next run collides with orphaned daemons still holding the ports.
    void StopAll();

    const std::vector<mp1::Machine>& machines() const { return machines_; }
    const std::string& log_dir() const { return log_dir_; }

    // Ground truth for the logs Start() generated: token -> machine -> lines.
    const mp1::ExpectedCounts& expected() const { return expected_; }

    // What one machine should return for `token`, and what the cluster should
    // in total. Thin wrappers over `expected`, but they let a test read as an
    // assertion instead of as a pair of nested map lookups.
    uint64_t ExpectedOn(const std::string& token, int machine_id) const;
    uint64_t ExpectedTotal(const std::string& token) const;

    ~Cluster() { StopAll(); }

private:
    std::vector<mp1::Machine> machines_;
    std::vector<int>          pids_;      // -1 once a daemon has been reaped
    std::string               log_dir_;
    mp1::ExpectedCounts       expected_;
};

}  // namespace mp1test
