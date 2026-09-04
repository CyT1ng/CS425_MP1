#pragma once
//
// Deterministic log generator.
//
// This is test infrastructure, not a toy: the unit tests in Part II can only
// "automatically verify that the results are what you expect" if something knows
// the ground truth. That something is this file. Given the same seed and spec,
// it must produce byte-identical logs and an exact expected match count for
// every pattern -- on your laptop and on the VMs.
//
// It also generates the 60 MB x 4 logs the report's measurements need.
//
#include <cstdint>
#include <string>
#include <vector>

namespace mp1 {

// One planted pattern with known ground truth.
struct PatternSpec {
    std::string token;             // literal token injected into chosen lines
    std::vector<int> machine_ids;  // which machines get it: one / some / all
    double frequency = 0.0;        // fraction of that machine's lines, 0..1
};

struct LogSpec {
    int      machine_id  = 0;
    uint64_t line_count  = 0;   // use this OR target_bytes
    uint64_t target_bytes = 0;  // e.g. 60 MB for the report runs
    uint64_t seed        = 0;   // same seed => same file, always
    std::vector<PatternSpec> patterns;
};

// Ground truth the tests assert against.
struct ExpectedCounts {
    // token -> per-machine expected matching-line count
    std::vector<std::pair<std::string, std::vector<std::pair<int, uint64_t>>>> by_token;

    // TODO: total across machines for `token`.
    uint64_t TotalFor(const std::string& token) const;
};

// TODO: write the log file for `spec` and fill in `expected`.
//
// Requirements that make the tests trustworthy:
//   - Deterministic: seed a std::mt19937_64 with spec.seed. Never use rand(),
//     never touch wall-clock time, or your logs differ between machines and the
//     expected counts become fiction.
//   - Realistic-looking lines: timestamp, level, component, message. It should
//     be plausible that this came from a real service.
//   - The filler text must NEVER accidentally contain a planted token, or your
//     expected counts will be short. Draw filler from a vocabulary that is
//     disjoint from the token set, and assert that invariant.
//   - Place planted tokens at exact computed positions rather than
//     probabilistically, so the expected count is exact and not statistical.
//   - Count LINES containing a token, not occurrences: two hits on one line is
//     one line to grep.
bool GenerateLog(const LogSpec& spec, const std::string& out_path,
                 ExpectedCounts& expected, std::string* err);

// Convenience specs so the tests and the measurement scripts agree on what
// "rare" / "somewhat frequent" / "frequent" mean. State these numbers in the
// report -- "frequent" is meaningless to a grader without them.
//
//   kRare      ~ 1e-5  of lines  (a handful of matches cluster-wide)
//   kSomewhat  ~ 1e-3  of lines
//   kFrequent  ~ 1e-1  of lines  (large result set; network-bound)
inline constexpr double kRareFrequency      = 0.00001;
inline constexpr double kSomewhatFrequency  = 0.001;
inline constexpr double kFrequentFrequency  = 0.1;

}  // namespace mp1
