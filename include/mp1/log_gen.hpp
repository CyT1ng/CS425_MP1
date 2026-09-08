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
#include <map>
#include <string>
#include <vector>

namespace mp1 {

// One planted pattern with known ground truth.
struct PatternSpec {
    std::string token;             // literal token injected into chosen lines
    std::vector<int> machine_ids;  // which machines get it: one / some / all
    // Fraction of that machine's lines, 0..1. A non-zero frequency that rounds
    // down to no lines at all still plants one: "rare" has to mean rare, not
    // absent, or the rare-pattern test asserts 0 == 0 and proves nothing.
    double frequency = 0.0;
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
    // token -> (machine id -> expected number of MATCHING LINES on that machine)
    std::map<std::string, std::map<int, uint64_t>> by_token;

    // Sums across machines for `token`. Returns 0 for an unknown token.
    uint64_t TotalFor(const std::string& token) const;
};

// Writes the log file for `spec` and records its ground truth in `expected`,
// which is ADDED to rather than cleared -- so all ten machines can be generated
// into one ExpectedCounts and asserted against cluster-wide totals.
//
// What makes the resulting counts trustworthy:
//   - Deterministic. A std::mt19937_64 seeded from spec.seed and the machine
//     id; never rand(), never the wall clock. Otherwise two runs disagree and
//     every expected count is fiction.
//   - Realistic lines: timestamp, level, component, message, so the demo looks
//     like it is searching a service's log, because it is.
//   - Planted tokens go at computed positions, not probabilistically, so an
//     expected count is an exact number rather than a distribution.
//   - LINES containing a token are counted, not occurrences: two hits on one
//     line is one line to grep.
//   - The counts are then taken from the finished lines themselves and checked
//     against the plan, so a filler word that ever collided with a token fails
//     here loudly instead of quietly making every expectation too low.
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

// The tokens log-gen plants by default -- the ones the test suite and
// scripts/measure.sh query by name. They live here so the generator, the tests
// and the measurement script cannot drift apart: a token typo in one of three
// copies would look exactly like a correctness bug in the querier.
//
// Every token is upper case with an underscore and a hex tail, and the filler
// vocabulary is lower-case words, so a token cannot turn up in filler text by
// accident. GenerateLog verifies that rather than trusting it.
inline constexpr char kRareToken[]     = "RARE_TOKEN_7f3a";
inline constexpr char kSomewhatToken[] = "SOMEWHAT_TOKEN_b21c";
inline constexpr char kFrequentToken[] = "FREQUENT_TOKEN_e50d";
inline constexpr char kOneLogToken[]   = "ONELOG_TOKEN_d17c";    // machine 3 only
inline constexpr char kSomeLogsToken[] = "SOMELOGS_TOKEN_9ab2";  // odd machines
inline constexpr char kAbsentToken[]   = "ABSENT_TOKEN_0f00";    // never planted

// The pattern set log-gen plants by default: one per frequency class, plus one
// that lands on a single machine and one that lands on a subset. A freshly
// deployed cluster can then demonstrate every axis the spec asks about -- rare
// / somewhat frequent / frequent, and one / some / all logs -- with no second
// generation step and nothing to remember at the demo.
std::vector<PatternSpec> DefaultPatterns();

}  // namespace mp1
