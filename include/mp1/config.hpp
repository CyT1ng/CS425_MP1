#pragma once
//
// Cluster membership. The spec explicitly allows hard state, so the machine
// list is a static config file rather than anything discovered at runtime.
//
// config/machines.txt is the VM cluster, config/local.txt the several-
// processes-on-one-host development cluster; both use the same format (see
// either file for a worked example):
//   <id> <host> <port>
// Blank lines and lines beginning with '#' are ignored.
//
#include <cstdint>
#include <string>
#include <vector>

namespace mp1 {

struct Machine {
    int         id;    // VM number i; log file is "machine.<id>.log"
    std::string host;
    uint16_t    port;
};

// Parse `path` into `out`, replacing whatever `out` held.
//
// Returns true on success. On the first problem found, returns false, writes a
// message to `err` (when non-null), and leaves `out` EMPTY rather than half
// populated -- a caller that ignores the return value must not end up quietly
// querying a subset of the cluster.
//
// Tolerated: blank lines, leading and trailing whitespace, and '#' comment
// lines. Skip exactly what deploy.sh skips with `grep -vE '^\s*(#|$)'`,
// leading whitespace before the '#' included. If the shell and the daemon
// disagree about which lines are live, the cluster half-starts.
//
// Rejected, each reported against its line number:
//   - anything other than three whitespace-separated fields. Trailing '#'
//     comments count as malformed; they break deploy.sh's `read` too.
//   - id <= 0, or an id already claimed by an earlier line.
//   - a port outside 1..65535. Range-check before narrowing: stoi("70000")
//     succeeds and truncates to 4464, binding a port nobody will call.
//   - a non-numeric id or port. stoi THROWS on those -- let it escape and a
//     typo kills the daemon instead of producing the message below.
//   - a file that parses cleanly but yields zero machines, which would leave
//     dgrep fanning out to nobody and exiting 0: indistinguishable from a
//     pattern that legitimately matched nothing.
//
// Hostnames are NOT resolved here. DNS belongs at connect time, where
// MachineStatus::kUnreachable already accounts for it; resolving during parse
// turns a config typo into a timeout and makes startup depend on the network.
//
// Report failures loudly and specifically -- a typo here otherwise surfaces as
// a mysterious "machine unreachable" at demo time, which is a bad way to find
// out. Name the file, the line, and the offending value:
//
//   config/machines.txt:7: duplicate id 3 (first seen on line 5)
//
// Both callers default to a RELATIVE path (deploy.sh:14, dgrep --config), so a
// missing file usually means the wrong working directory. Say that outright.
//
// TODO: implement; currently a stub that always fails.
bool LoadMachines(const std::string& path, std::vector<Machine>& out,
                  std::string* err);

// Returns "machine.<id>.log" -- the log naming convention the spec fixes.
//
// Total: every int maps to a name, so there is no failure to report. Callers
// join it onto a directory themselves (server_main.cpp does
// `log_dir + "/" + LogFileName(id)`); this deliberately returns a bare
// filename, not a path.
//
// It exists as a shared function rather than a sprintf at each use site
// because three binaries have to agree on it: mp1d greps the file, mp1gen
// writes it, and the tests assert on it. Three copies would eventually drift,
// and the symptom -- a daemon serving a file the generator never created --
// looks like a dead machine rather than a naming bug.
std::string LogFileName(int id);

}  // namespace mp1
