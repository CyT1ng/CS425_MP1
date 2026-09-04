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
// Returns true on success. On the first bad line it returns false, writes a
// message to `err` (when non-null), and leaves `out` EMPTY -- never partly
// filled, so a caller that ignores the return value cannot end up quietly
// querying a subset of the cluster. There is no partial success: one bad line
// means zero machines, not the nine that parsed.
//
// Ignored: blank lines, and '#' comments with any leading whitespace. This has
// to match what deploy.sh skips with `grep -vE '^\s*(#|$)'` -- if the shell and
// the daemon disagree about which lines are live, the cluster half-starts.
//
// Rejected, each reported against its line number:
//   - a line that is not three whitespace-separated fields
//   - a non-numeric id or port
//   - id <= 0, or an id an earlier line already claimed
//   - a port outside 1..65535. Range-check BEFORE narrowing to uint16_t, or
//     70000 silently becomes 4464 and the daemon binds a port nobody calls.
//   - a file that parses cleanly but defines no machines, which would leave
//     dgrep fanning out to nobody and exiting 0 -- indistinguishable from a
//     pattern that legitimately had no matches
//
// Hostnames are NOT resolved here, so a machine in the list is well-formed, not
// known to be up. DNS belongs at connect time, where MachineStatus::kUnreachable
// already covers it; resolving during the parse would turn a config typo into a
// timeout and make startup depend on the network.
//
// Errors name the file, the line, and the offending value:
//
//   config/machines.txt:7: duplicate id 3
//
// A typo here otherwise surfaces as a mysterious "machine unreachable" at demo
// time, which is a bad way to find out. Both callers default to a RELATIVE path
// (dgrep --config, deploy.sh:14), so a missing file usually means nothing worse
// than the wrong working directory -- say so plainly.
bool LoadMachines(const std::string& path, std::vector<Machine>& out,
                  std::string* err);

// Returns "machine.<id>.log", the log naming convention the spec fixes.
//
// Always succeeds; every id maps to a name. Returns a bare filename rather than
// a path -- callers join it onto a directory themselves, as server_main.cpp
// does with `log_dir + "/" + LogFileName(id)`.
//
// It is a shared function rather than a format string repeated at each use site
// because three binaries have to agree on it: mp1d greps the file, mp1gen
// writes it, and the tests assert on it. Separate copies would drift, and the
// symptom -- a daemon serving a file the generator never created -- reads as a
// dead machine rather than a naming bug.
std::string LogFileName(int id);

}  // namespace mp1
