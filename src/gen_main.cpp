// mp1gen -- generates machine.<id>.log with known, reproducible contents.
//
//   mp1gen --id <n> --seed <s> [--lines <n> | --bytes <n>] [--out-dir .]
//
// Two jobs:
//   1. Feed the distributed unit tests logs whose exact match counts are known
//      in advance, so the tests can assert instead of eyeball.
//   2. Build the 4 x 60 MB logs the report's latency measurements need.
//
// The VMs have no persistent storage, so this gets re-run after every reboot.
// Keep it fast and keep it deterministic: same --seed and --id must always
// produce a byte-identical file, or your expected counts stop meaning anything.

#include <cstdio>

#include "mp1/log_gen.hpp"

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    std::fprintf(stderr,
                 "usage: mp1gen --id <n> --seed <s> "
                 "[--lines <n> | --bytes <n>] [--out-dir <dir>]\n");
    // TODO: parse args, build a LogSpec (patterns at the rare / somewhat /
    // frequent frequencies from log_gen.hpp), call GenerateLog, and print the
    // expected counts to stdout so scripts and tests can consume them.
    return 1;
}
