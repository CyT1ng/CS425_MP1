#pragma once
//
// Cluster membership. The spec explicitly allows hard state, so the machine
// list is a static config file rather than anything discovered at runtime.
//
// config/machines.txt format (see that file for a worked example):
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

// TODO: parse the file. Reject duplicate ids and malformed lines loudly at
// startup -- a typo here surfaces as a mysterious "machine unreachable" at demo
// time, which is a bad way to find out.
bool LoadMachines(const std::string& path, std::vector<Machine>& out,
                  std::string* err);

// TODO: return "machine.<id>.log". Single definition so the server, the log
// generator, and the tests can never disagree about the naming convention.
std::string LogFileName(int id);

}  // namespace mp1
