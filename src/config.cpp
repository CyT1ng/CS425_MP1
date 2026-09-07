#include "mp1/config.hpp"

#include <fstream>
#include <sstream>

namespace mp1 {
namespace {

// "config/machines.txt:7: " -- the prefix on every error message.
std::string Where(const std::string& path, int lineno) {
    return path + ":" + std::to_string(lineno) + ": ";
}

// A line is skippable if it is blank or its first non-space character is '#'.
bool IsBlankOrComment(const std::string& line) {
    size_t i = line.find_first_not_of(" \t");
    return i == std::string::npos || line[i] == '#';
}

bool HasId(const std::vector<Machine>& machines, int id) {
    for (const Machine& m : machines) {
        if (m.id == id) return true;
    }
    return false;
}

// Parse one "<id> <host> <port>" line. On bad input, returns false and puts the
// reason in `why` -- the caller adds the file and line number.
bool ParseLine(const std::string& line, Machine* out, std::string* why) {
    std::istringstream ss(line);
    int id, port;
    std::string host;

    // Reading into an int fails on non-numeric input, so this one check covers
    // both "wrong number of fields" and "that is not a number".
    if (!(ss >> id >> host >> port)) {
        *why = "expected \"<id> <host> <port>\", got: " + line;
        return false;
    }
    if (id <= 0) {
        *why = "id must be positive";
        return false;
    }
    // Range-check before narrowing, or 70000 quietly becomes 4464.
    if (port < 1 || port > 65535) {
        *why = "port out of range: " + std::to_string(port);
        return false;
    }

    out->id   = id;
    out->host = host;
    out->port = static_cast<uint16_t>(port);
    return true;
}

}  // namespace

// Reads `path` into `out`; the full contract is in config.hpp.
//
// The per-line work lives in the helpers above -- IsBlankOrComment decides what
// to skip, ParseLine validates one line, HasId catches a repeat. What is left
// here is the file itself: open it, number the lines so errors can point at
// one, and commit all-or-nothing at the end.
//
// Machines accumulate in a local vector and reach `out` only once the whole
// file has parsed, which is what keeps the "empty on failure" promise without a
// clear() on every error path.
bool LoadMachines(const std::string& path, std::vector<Machine>& out,
                  std::string* err) {
    out.clear();

    std::ifstream f(path);
    if (!f) {
        if (err) *err = path + ": cannot open";
        return false;
    }

    std::vector<Machine> parsed;
    std::string line;
    int lineno = 0;

    while (std::getline(f, line)) {
        ++lineno;
        if (IsBlankOrComment(line)) continue;

        Machine machine;
        std::string why;
        if (!ParseLine(line, &machine, &why)) {
            if (err) *err = Where(path, lineno) + why;
            return false;
        }
        if (HasId(parsed, machine.id)) {
            if (err) *err = Where(path, lineno) + "duplicate id " +
                            std::to_string(machine.id);
            return false;
        }
        parsed.push_back(machine);
    }

    if (parsed.empty()) {
        if (err) *err = path + ": no machines defined";
        return false;
    }

    out = parsed;
    return true;
}

// Returns "machine.<id>.log".
//
// Trivial on purpose. The value is not the string concatenation, it is that
// log-server, log-gen and the tests all read the naming rule from one place.
std::string LogFileName(int id) {
    return "machine." + std::to_string(id) + ".log";
}

}  // namespace mp1
