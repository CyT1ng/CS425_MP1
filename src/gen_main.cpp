// log-gen -- generates machine.<id>.log with known, reproducible contents.
//
//   log-gen --id <n> --seed <s> [--lines <n> | --bytes <n>] [--out-dir .]
//
// Two jobs:
//   1. Feed the distributed unit tests logs whose exact match counts are known
//      in advance, so the tests can assert instead of eyeball.
//   2. Build the 4 x 60 MB logs the report's latency measurements need.
//
// The VMs have no persistent storage, so this gets re-run after every reboot.
// Same --seed and --id always produce a byte-identical file, or the expected
// counts stop meaning anything.
//
// The expected counts go to stdout as "EXPECT" lines, one per token, so a
// script can compare them against what log-query reports:
//
//   EXPECT machine=1 token=RARE_TOKEN_7f3a lines=3

#include <cstdio>
#include <cstdlib>
#include <string>

#include "mp1/config.hpp"
#include "mp1/log_gen.hpp"

namespace {

void Usage() {
    std::fprintf(stderr,
                 "usage: log-gen --id <n> --seed <s> "
                 "[--lines <n> | --bytes <n>] [--out-dir <dir>]\n");
}

// A numeric flag value, or a clear death. --lines quietly parsing as 0 would
// produce an empty log and a test suite that fails somewhere else entirely.
unsigned long long RequireNumber(const char* flag, const char* value,
                                 unsigned long long low,
                                 unsigned long long high) {
    const std::string text = value ? value : "";
    const bool numeric = !text.empty() && text.size() <= 18 &&
                         text.find_first_not_of("0123456789") == std::string::npos;
    const unsigned long long number = numeric ? std::stoull(text) : 0;
    if (!numeric || number < low || number > high) {
        std::fprintf(stderr, "log-gen: %s expects a number in %llu..%llu, got \"%s\"\n",
                     flag, low, high, text.c_str());
        std::exit(2);
    }
    return number;
}

}  // namespace

int main(int argc, char** argv) {
    mp1::LogSpec spec;
    spec.patterns       = mp1::DefaultPatterns();
    std::string out_dir = ".";

    for (int i = 1; i < argc; ++i) {
        const std::string flag  = argv[i];
        const char*       value = (i + 1 < argc) ? argv[i + 1] : nullptr;

        if (flag == "--id") {
            spec.machine_id = static_cast<int>(RequireNumber("--id", value, 1, 100000));
            ++i;
        } else if (flag == "--seed") {
            spec.seed = RequireNumber("--seed", value, 0, ~0ull >> 1);
            ++i;
        } else if (flag == "--lines") {
            spec.line_count = RequireNumber("--lines", value, 1, 1ull << 40);
            ++i;
        } else if (flag == "--bytes") {
            spec.target_bytes = RequireNumber("--bytes", value, 1, 1ull << 40);
            ++i;
        } else if (flag == "--out-dir" && value) {
            out_dir = value;
            ++i;
        } else {
            std::fprintf(stderr, "log-gen: unrecognised argument: %s\n", flag.c_str());
            Usage();
            return 2;
        }
    }

    if (spec.machine_id == 0) {
        std::fprintf(stderr, "log-gen: --id is required\n");
        Usage();
        return 2;
    }
    if (spec.line_count == 0 && spec.target_bytes == 0) {
        std::fprintf(stderr, "log-gen: give either --lines or --bytes\n");
        Usage();
        return 2;
    }

    const std::string path = out_dir + "/" + mp1::LogFileName(spec.machine_id);

    mp1::ExpectedCounts expected;
    std::string err;
    if (!mp1::GenerateLog(spec, path, expected, &err)) {
        std::fprintf(stderr, "log-gen: %s\n", err.c_str());
        return 1;
    }

    std::fprintf(stderr, "log-gen: wrote %s\n", path.c_str());
    for (const auto& token_entry : expected.by_token) {
        for (const auto& machine_entry : token_entry.second) {
            std::printf("EXPECT machine=%d token=%s lines=%llu\n",
                        machine_entry.first, token_entry.first.c_str(),
                        static_cast<unsigned long long>(machine_entry.second));
        }
    }
    return 0;
}
