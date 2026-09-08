#include "mp1/log_gen.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <random>

namespace mp1 {
namespace {

// The filler vocabulary: lower-case words, nothing else. That is the whole
// reason a planted token can never turn up in filler text by accident -- every
// token is upper case with an underscore. GenerateLog still checks rather than
// trusting it, but this is why the check passes.
const char* const kWords[] = {
    "connection", "refused", "timeout", "retry", "budget", "exhausted",
    "leader", "follower", "election", "heartbeat", "quorum", "commit",
    "snapshot", "compaction", "flush", "cache", "evicted", "stale",
    "handshake", "renegotiated", "checksum", "mismatch", "replay",
    "backlog", "throttled", "queue", "depth", "latency", "spike",
    "session", "expired", "rebalanced", "segment", "sealed", "reopened",
};

const char* const kComponents[] = {
    "db", "net", "raft", "cache", "api", "disk", "sched", "auth",
};

// Severity, and how often each one shows up. Weighted rather than uniform so a
// query for ERROR behaves the way it would against a real service's log.
struct Level { const char* name; int weight; };
const Level kLevels[] = {
    {"DEBUG", 40}, {"INFO", 35}, {"WARN", 17}, {"ERROR", 8},
};

// 2026-09-13T00:00:00Z. A fixed starting point, never the clock: reading the
// clock would make two runs of the same seed differ, and every expected count
// in the test suite is only as trustworthy as this.
constexpr std::time_t kStartTime = 1789257600;

// Timestamps advance at a steady eight lines per second, so they stay ordered
// and look like a real log without consuming any randomness.
constexpr uint64_t kLinesPerSecond = 8;

template <typename T, size_t N>
constexpr size_t Count(const T (&)[N]) { return N; }

// Mixes the caller's seed with the machine id (splitmix64), so one --seed
// produces a DIFFERENT log on each machine while every machine stays exactly
// reproducible on its own.
uint64_t SeedFor(const LogSpec& spec) {
    uint64_t h = spec.seed + 0x9E3779B97F4A7C15ull;
    h ^= static_cast<uint64_t>(spec.machine_id) * 0xBF58476D1CE4E5B9ull;
    h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ull;
    h = (h ^ (h >> 27)) * 0x94D049BB133111EBull;
    return h ^ (h >> 31);
}

const char* PickLevel(std::mt19937_64& rng) {
    int roll = static_cast<int>(rng() % 100);
    for (const Level& level : kLevels) {
        roll -= level.weight;
        if (roll < 0) return level.name;
    }
    return kLevels[0].name;  // only if the weights stop summing to 100
}

// One log line without its newline, shaped like something a real service would
// print -- a log that looks nothing like a log makes a poor demo:
//
//   2026-09-13T04:12:01.375Z WARN  [db] retry budget exhausted shard=41 req=8c1f2a
void AppendLine(std::mt19937_64& rng, uint64_t index, std::string* out) {
    const std::time_t seconds =
        kStartTime + static_cast<std::time_t>(index / kLinesPerSecond);
    const unsigned millis =
        static_cast<unsigned>((index % kLinesPerSecond) * (1000 / kLinesPerSecond));

    std::tm utc;
    ::gmtime_r(&seconds, &utc);

    // snprintf with an explicit format rather than strftime: no locale can get
    // in the way, so the bytes are the same on a laptop and on the VMs.
    char stamp[40];
    std::snprintf(stamp, sizeof(stamp), "%04d-%02d-%02dT%02d:%02d:%02d.%03uZ",
                  utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                  utc.tm_hour, utc.tm_min, utc.tm_sec, millis);

    *out += stamp;
    *out += ' ';
    *out += PickLevel(rng);
    *out += " [";
    *out += kComponents[rng() % Count(kComponents)];
    *out += "] ";

    const int words = 3 + static_cast<int>(rng() % 5);
    for (int i = 0; i < words; ++i) {
        if (i > 0) *out += ' ';
        *out += kWords[rng() % Count(kWords)];
    }

    char fields[48];
    std::snprintf(fields, sizeof(fields), " shard=%u req=%06x",
                  static_cast<unsigned>(rng() % 64),
                  static_cast<unsigned>(rng() % 0x1000000));
    *out += fields;
}

// A token's own starting offset within its first bucket (FNV-1a). Two patterns
// planted at the same frequency would otherwise compute identical positions and
// land on exactly the same lines, which is both unrealistic and a waste of a
// test axis -- every "some logs" line would also carry the "somewhat frequent"
// token. Derived from the token, so it stays reproducible.
uint64_t TokenPhase(const std::string& token) {
    uint64_t hash = 1469598103934665603ull;
    for (const char c : token) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ull;
    }
    return hash;
}

bool AppliesTo(const PatternSpec& pattern, int machine_id) {
    return std::find(pattern.machine_ids.begin(), pattern.machine_ids.end(),
                     machine_id) != pattern.machine_ids.end();
}

// Turns a byte target into a line count.
//
// The planting positions have to be computed from the total up front -- that is
// what makes the expected counts exact rather than statistical -- so the file
// cannot simply be stopped once it is big enough. Line lengths vary, so instead
// of guessing an average, a short sample is generated with a throwaway RNG and
// measured. Deterministic, cheap, and a 60 MB target lands within about 1%.
uint64_t EstimateLineCount(const LogSpec& spec,
                           const std::vector<const PatternSpec*>& planted) {
    constexpr uint64_t kSampleLines = 4096;

    std::mt19937_64 rng(SeedFor(spec));
    std::string line;
    uint64_t bytes = 0;
    for (uint64_t i = 0; i < kSampleLines; ++i) {
        line.clear();
        AppendLine(rng, i, &line);
        bytes += line.size() + 1;  // + the newline
    }

    double per_line = static_cast<double>(bytes) / kSampleLines;
    for (const PatternSpec* pattern : planted) {
        per_line += pattern->frequency * (pattern->token.size() + 1);
    }
    return std::max<uint64_t>(
        1, static_cast<uint64_t>(spec.target_bytes / per_line));
}

}  // namespace

uint64_t ExpectedCounts::TotalFor(const std::string& token) const {
    const auto it = by_token.find(token);
    if (it == by_token.end()) return 0;

    uint64_t total = 0;
    for (const auto& per_machine : it->second) total += per_machine.second;
    return total;
}

std::vector<PatternSpec> DefaultPatterns() {
    // Ten is the demo's cluster size, so the subset patterns are defined over
    // ids 1..10 and simply do not apply on a machine outside that range.
    const std::vector<int> all       = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    const std::vector<int> odd       = {1, 3, 5, 7, 9};
    const std::vector<int> machine_3 = {3};

    return {
        {kRareToken,      all,       kRareFrequency},
        {kSomewhatToken,  all,       kSomewhatFrequency},
        {kFrequentToken,  all,       kFrequentFrequency},
        {kOneLogToken,    machine_3, kSomewhatFrequency},
        {kSomeLogsToken,  odd,       kSomewhatFrequency},
    };
}

// Writes one machine's log and records the ground truth for it.
//
// `expected` is ADDED to, never cleared, so a caller can generate all ten
// machines into one ExpectedCounts and then assert against cluster-wide totals.
bool GenerateLog(const LogSpec& spec, const std::string& out_path,
                 ExpectedCounts& expected, std::string* err) {
    // Which patterns land on THIS machine, and the full token list -- every
    // token is counted below, including the ones planted elsewhere, so that a
    // machine which should have none can be shown to have exactly none.
    std::vector<std::string> tokens;
    std::vector<const PatternSpec*> planted;
    for (const PatternSpec& pattern : spec.patterns) {
        if (pattern.token.empty()) {
            if (err) *err = "a pattern has an empty token";
            return false;
        }
        tokens.push_back(pattern.token);
        if (pattern.frequency > 0.0 && AppliesTo(pattern, spec.machine_id)) {
            planted.push_back(&pattern);
        }
    }

    // One token inside another would make both counts wrong, and the symptom
    // would look like a bug in the querier rather than in the test data.
    for (size_t a = 0; a < tokens.size(); ++a) {
        for (size_t b = 0; b < tokens.size(); ++b) {
            if (a != b && tokens[a].find(tokens[b]) != std::string::npos) {
                if (err) {
                    *err = "token \"" + tokens[b] + "\" occurs inside \"" +
                           tokens[a] + "\"; counts would overlap";
                }
                return false;
            }
        }
    }

    if (spec.line_count == 0 && spec.target_bytes == 0) {
        if (err) *err = "set either line_count or target_bytes";
        return false;
    }
    const uint64_t line_count = spec.line_count > 0
                                    ? spec.line_count
                                    : EstimateLineCount(spec, planted);

    // How many lines each planted token gets, and which ones.
    //
    // Positions are computed, not drawn: the file is cut into n equal buckets
    // and the token goes at the same point inside each one, so the k-th of n
    // planted lines is line k * line_count / n + phase. Even spacing makes the
    // expected count an exact number rather than a distribution -- the
    // difference between a test that asserts and a test that hopes -- and the
    // per-token phase keeps patterns off line 0 and off each other.
    std::vector<uint64_t> quota(planted.size());
    std::vector<uint64_t> phase(planted.size());
    std::vector<uint64_t> next_line(planted.size());
    std::vector<uint64_t> placed(planted.size(), 0);
    for (size_t i = 0; i < planted.size(); ++i) {
        quota[i] = static_cast<uint64_t>(planted[i]->frequency * line_count + 0.5);
        // A frequency that rounds down to nothing still plants one line: "rare"
        // has to mean rare, not absent, or the rare-pattern test asserts 0 == 0
        // and proves nothing.
        quota[i] = std::min(std::max<uint64_t>(quota[i], 1), line_count);

        // Somewhere inside the first bucket, and never past its end, so the
        // positions stay strictly increasing and the last one stays in the file.
        const uint64_t bucket = line_count / quota[i];
        phase[i]     = TokenPhase(planted[i]->token) % bucket;
        next_line[i] = phase[i];
    }

    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (err) *err = out_path + ": cannot open for writing";
        return false;
    }

    std::mt19937_64 rng(SeedFor(spec));
    std::vector<uint64_t> observed(tokens.size(), 0);

    std::string line;
    std::string buffer;
    constexpr size_t kFlushAt = 1u << 20;  // one write() per MB, not per line
    buffer.reserve(kFlushAt + 4096);

    for (uint64_t index = 0; index < line_count; ++index) {
        line.clear();
        AppendLine(rng, index, &line);

        for (size_t i = 0; i < planted.size(); ++i) {
            if (index != next_line[i] || placed[i] >= quota[i]) continue;
            line += ' ';
            line += planted[i]->token;
            ++placed[i];
            next_line[i] = placed[i] < quota[i]
                               ? placed[i] * line_count / quota[i] + phase[i]
                               : line_count;  // done: never matches again
        }

        // Ground truth is COUNTED off the finished line, not assumed from the
        // plan. If filler text ever collided with a token, the mismatch check
        // below catches it here rather than letting every expected count in the
        // distributed suite be quietly too low.
        for (size_t t = 0; t < tokens.size(); ++t) {
            if (line.find(tokens[t]) != std::string::npos) ++observed[t];
        }

        buffer += line;
        buffer += '\n';
        if (buffer.size() >= kFlushAt) {
            out.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            buffer.clear();
        }
    }
    out.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    out.close();
    if (!out) {
        if (err) *err = out_path + ": write failed (disk full?)";
        return false;
    }

    // The plan said how many lines each token should be on. Anything else means
    // the filler vocabulary and the tokens are no longer disjoint.
    for (size_t t = 0; t < tokens.size(); ++t) {
        uint64_t wanted = 0;
        for (size_t i = 0; i < planted.size(); ++i) {
            if (planted[i]->token == tokens[t]) wanted = quota[i];
        }
        if (observed[t] != wanted) {
            if (err) {
                *err = "token \"" + tokens[t] + "\" landed on " +
                       std::to_string(observed[t]) + " lines but " +
                       std::to_string(wanted) + " were planted; the filler "
                       "vocabulary and the token set are not disjoint";
            }
            return false;
        }
        expected.by_token[tokens[t]][spec.machine_id] = observed[t];
    }
    return true;
}

}  // namespace mp1
