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
#include <string>
#include <vector>

#include "mp1/config.hpp"

namespace mp1test {

class Cluster {
public:
    // TODO: generate logs for ids 1..n in a temp dir, fork+exec one log-server
    // per machine on ports base_port+i, and wait until each is accepting
    // connections. Poll for readiness -- do NOT sleep for a fixed duration and
    // hope. Fixed sleeps are how test suites become flaky on a loaded VM.
    bool Start(int n, uint16_t base_port, std::string* err);

    // TODO: SIGKILL one machine's daemon to simulate a fail-stop failure.
    // SIGKILL, not SIGTERM: a clean shutdown could close the socket politely
    // and exercise a different path than a VM that simply vanishes.
    bool Kill(int machine_id, std::string* err);

    // TODO: kill everything and remove the temp dir. Must run even when a test
    // fails, or a rerun collides with orphaned daemons still holding the ports.
    void StopAll();

    const std::vector<mp1::Machine>& machines() const { return machines_; }
    const std::string& log_dir() const { return log_dir_; }

    ~Cluster() { StopAll(); }

private:
    std::vector<mp1::Machine> machines_;
    std::vector<int>          pids_;
    std::string               log_dir_;
};

}  // namespace mp1test
