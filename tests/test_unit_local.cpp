// Local unit tests: the pieces that need no sockets.
//
// These come FIRST. Every one of these bugs is far cheaper to find here than as
// a mysterious hang across ten VMs.

#include "test_framework.hpp"

#include <sys/socket.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "mp1/config.hpp"
#include "mp1/grep_runner.hpp"
#include "mp1/log_gen.hpp"
#include "mp1/protocol.hpp"

namespace {

// --- helpers --------------------------------------------------------------

// A connected pair of Conns over socketpair(): the real read and write loops
// and the real wire format, with no ports, no daemons, and no chance of
// colliding with something else on the machine.
struct Pair {
    mp1::Conn client;
    mp1::Conn server;
    bool      ok = false;
};

Pair MakePair() {
    int fds[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) return Pair{};

    Pair pair;
    pair.client = mp1::Conn(fds[0]);
    pair.server = mp1::Conn(fds[1]);
    pair.ok     = true;
    return pair;
}

// A scratch directory that cleans up after itself. The grep tests write real
// files because they run the real grep on real paths -- there is nothing to
// fake there, and no reason to want to.
class Scratch {
public:
    Scratch() {
        char dir_template[] = "/tmp/mp1_unit_XXXXXX";
        if (::mkdtemp(dir_template) != nullptr) dir_ = dir_template;
    }

    ~Scratch() {
        for (const std::string& name : files_) {
            ::unlink((dir_ + "/" + name).c_str());
        }
        if (!dir_.empty()) ::rmdir(dir_.c_str());
    }

    Scratch(const Scratch&)            = delete;
    Scratch& operator=(const Scratch&) = delete;

    bool ok() const { return !dir_.empty(); }
    const std::string& dir() const { return dir_; }

    // Registers `name` for cleanup and returns its full path. The file does not
    // have to exist yet -- the log generator and grep both create their own.
    std::string Path(const std::string& name) {
        files_.push_back(name);
        return dir_ + "/" + name;
    }

    std::string WriteFile(const std::string& name, const std::string& contents) {
        const std::string path = Path(name);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        return path;
    }

private:
    std::string              dir_;
    std::vector<std::string> files_;
};

// Runs grep the way log-server does -- streaming through a sink -- and collects
// what it printed. `ran` is RunGrep's own return value: whether grep could be
// run at all, which is a different question from what grep then said.
mp1::GrepResult Grep(const std::vector<std::string>& args,
                     const std::string& path, std::string* output, bool* ran) {
    mp1::GrepResult result;
    std::string err;
    output->clear();
    *ran = mp1::RunGrep(args, path,
                        [output](const std::string& chunk) {
                            *output += chunk;
                            return true;
                        },
                        &result, &err);
    if (!*ran) std::printf("  (RunGrep failed: %s)\n", err.c_str());
    return result;
}

// The line count grep reports for `args`, ignoring its output.
uint64_t GrepLines(const std::vector<std::string>& args, const std::string& path) {
    std::string output;
    bool ran = false;
    const mp1::GrepResult result = Grep(args, path, &output, &ran);
    return ran ? result.line_count : 0;
}

}  // namespace

// --- protocol -------------------------------------------------------------
//
// These run over a socketpair(), so they exercise the real Conn read/write
// loops and the real wire format without any network, ports, or daemons.

TEST(Conn_ReadLineHandlesSplitAndCoalescedReads) {
    Pair pair = MakePair();
    REQUIRE(pair.ok, "socketpair failed");

    // Two writes that do not line up with the lines at all: the first stops
    // mid-line, the second finishes that line AND delivers a whole second one.
    // A reader that assumes one read() is one message fails immediately here.
    std::string err;
    CHECK(pair.client.WriteAll("AB", &err));
    CHECK(pair.client.WriteAll("C\nDEF\n", &err));

    std::string line;
    CHECK(pair.server.ReadLine(&line, &err));
    CHECK_EQ(line, std::string("ABC"));
    CHECK(pair.server.ReadLine(&line, &err));
    CHECK_EQ(line, std::string("DEF"));
}

TEST(Conn_ReadExactlyDetectsShortStream) {
    Pair pair = MakePair();
    REQUIRE(pair.ok, "socketpair failed");

    std::string err;
    CHECK(pair.client.WriteAll("0123456789", &err));
    pair.client = mp1::Conn();  // hang up: the destructor closes the socket

    // Ten bytes arrived and a hundred were promised. Handing back the ten as
    // though that were the whole message is silent corruption; this is exactly
    // what a peer dying mid-message looks like.
    std::string got;
    CHECK(!pair.server.ReadExactly(100, &got, &err));
}

TEST(Protocol_RequestRoundTrips) {
    Pair pair = MakePair();
    REQUIRE(pair.ok, "socketpair failed");

    const std::vector<std::string> argv = {
        "-i",                       // an ordinary flag
        "",                         // empty: a length of zero must still work
        "--color",                  // looks like one of log-query's own flags
        "sessão-não-encontrada",    // UTF-8, so bytes and characters differ
        "two\nlines",               // the entire reason arguments are
                                    // length-prefixed instead of one per line
        std::string(4096, 'x'),     // bigger than one read() will return
    };

    std::string err;
    CHECK(mp1::SendRequest(pair.client, mp1::Request{argv}, &err));

    mp1::Request received;
    CHECK(mp1::RecvRequest(pair.server, &received, &err));
    CHECK_EQ(received.argv.size(), argv.size());
    if (received.argv.size() == argv.size()) {
        for (size_t i = 0; i < argv.size(); ++i) CHECK_EQ(received.argv[i], argv[i]);
    }
}

TEST(Protocol_DataAndEndFramesRoundTrip) {
    Pair pair = MakePair();
    REQUIRE(pair.ok, "socketpair failed");

    // A line count above 2^32, which catches a count that got parsed or stored
    // in a 32-bit type somewhere along the way.
    const uint64_t kHugeCount = 5000000000ull;

    std::string err;
    CHECK(mp1::SendData(pair.server, "machine.1.log:first\n", &err));
    CHECK(mp1::SendData(pair.server, "machine.1.log:second\n", &err));
    CHECK(mp1::SendEnd(pair.server, 1, kHugeCount, &err));

    mp1::Frame frame;
    CHECK(mp1::RecvFrame(pair.client, &frame, &err));
    CHECK(!frame.is_end);
    CHECK_EQ(frame.data, std::string("machine.1.log:first\n"));

    CHECK(mp1::RecvFrame(pair.client, &frame, &err));
    CHECK(!frame.is_end);
    CHECK_EQ(frame.data, std::string("machine.1.log:second\n"));

    CHECK(mp1::RecvFrame(pair.client, &frame, &err));
    CHECK(frame.is_end);
    CHECK_EQ(frame.exit_code, 1);            // no match is a valid answer
    CHECK_EQ(frame.line_count, kHugeCount);
}

TEST(Protocol_RejectsGarbageHeader) {
    Pair pair = MakePair();
    REQUIRE(pair.ok, "socketpair failed");

    // What pointing log-query at the wrong port looks like. It has to say so,
    // not print another service's bytes as though they were grep output.
    std::string err;
    CHECK(pair.server.WriteAll("HELLO\n", &err));

    mp1::Frame frame;
    CHECK(!mp1::RecvFrame(pair.client, &frame, &err));
    CHECK(err.rfind(mp1::kWireErrorPrefix, 0) == 0);  // reported as malformed,
                                                      // which is what the client
                                                      // turns into kProtocolError
}

TEST(Protocol_TruncatedFrameIsDetected) {
    Pair pair = MakePair();
    REQUIRE(pair.ok, "socketpair failed");

    // A hundred bytes promised, ten delivered, then the peer vanishes. Handing
    // back the ten as a complete chunk is how a killed machine turns into a
    // plausible-looking wrong answer instead of a reported failure.
    std::string err;
    CHECK(pair.server.WriteAll("D 100\n0123456789", &err));
    pair.server = mp1::Conn();

    mp1::Frame frame;
    CHECK(!mp1::RecvFrame(pair.client, &frame, &err));
}

// --- config ---------------------------------------------------------------

TEST(LoadMachines_ParsesValidFile) {
    Scratch scratch;
    REQUIRE(scratch.ok(), "could not make a scratch directory");

    const std::string path = scratch.WriteFile("machines.txt",
        "# a comment\n"
        "\n"
        "   # an indented comment\n"
        "1   127.0.0.1   9401\n"
        "  2\t127.0.0.1\t9402  \n"      // tabs and trailing space
        "10  host.example.edu  4425\n");

    std::vector<mp1::Machine> machines;
    std::string err;
    REQUIRE(mp1::LoadMachines(path, machines, &err), err);

    CHECK_EQ(machines.size(), size_t{3});
    if (machines.size() == 3) {
        CHECK_EQ(machines[0].id, 1);
        CHECK_EQ(machines[0].host, std::string("127.0.0.1"));
        CHECK_EQ(machines[0].port, uint16_t{9401});
        CHECK_EQ(machines[1].id, 2);
        CHECK_EQ(machines[2].id, 10);
        CHECK_EQ(machines[2].host, std::string("host.example.edu"));
        CHECK_EQ(machines[2].port, uint16_t{4425});
    }
}

TEST(LoadMachines_RejectsDuplicateIds) {
    Scratch scratch;
    REQUIRE(scratch.ok(), "could not make a scratch directory");

    const std::string path = scratch.WriteFile("dupes.txt",
        "3 127.0.0.1 9401\n"
        "3 127.0.0.1 9402\n");

    // Two machines claiming id 3 would have them serving the same log file and
    // one of them silently shadowing the other in the summary table.
    std::vector<mp1::Machine> machines;
    std::string err;
    CHECK(!mp1::LoadMachines(path, machines, &err));
    CHECK(machines.empty());                       // never a partial cluster
    CHECK(err.find("duplicate id 3") != std::string::npos);
    CHECK(err.find(":2:") != std::string::npos);   // and it names the line
}

TEST(LoadMachines_RejectsMalformedLines) {
    Scratch scratch;
    REQUIRE(scratch.ok(), "could not make a scratch directory");

    // Each of these has to fail, and none may produce a partly filled list --
    // quietly querying a subset of the cluster is the worst possible outcome.
    const std::vector<std::pair<std::string, std::string>> bad = {
        {"missing_port.txt", "1 127.0.0.1\n"},
        {"not_a_number.txt", "one 127.0.0.1 9401\n"},
        {"port_too_big.txt", "1 127.0.0.1 70000\n"},   // would wrap to 4464
        {"port_zero.txt",    "1 127.0.0.1 0\n"},
        {"id_zero.txt",      "0 127.0.0.1 9401\n"},
        {"empty.txt",        "# nothing but comments\n"},
    };

    for (const auto& entry : bad) {
        const std::string path = scratch.WriteFile(entry.first, entry.second);
        std::vector<mp1::Machine> machines;
        std::string err;
        CHECK(!mp1::LoadMachines(path, machines, &err));
        CHECK(machines.empty());
        CHECK(!err.empty());
    }

    std::vector<mp1::Machine> machines;
    std::string err;
    CHECK(!mp1::LoadMachines(scratch.dir() + "/does_not_exist.txt", machines, &err));
}

TEST(LogFileName_MatchesSpec) {
    CHECK_EQ(mp1::LogFileName(1), std::string("machine.1.log"));
    CHECK_EQ(mp1::LogFileName(10), std::string("machine.10.log"));
}

// --- grep runner ----------------------------------------------------------

TEST(RunGrep_CountsLinesNotOccurrences) {
    Scratch scratch;
    REQUIRE(scratch.ok(), "could not make a scratch directory");

    // Two hits on one line is ONE line to grep, and the count the client checks
    // its received lines against has to agree.
    const std::string path = scratch.WriteFile("machine.1.log",
        "needle and another needle on one line\n"
        "nothing here\n");

    std::string output;
    bool ran = false;
    const mp1::GrepResult result = Grep({"needle"}, path, &output, &ran);
    CHECK(ran);
    CHECK_EQ(result.exit_code, 0);
    CHECK_EQ(result.line_count, uint64_t{1});
    CHECK(output.rfind("machine.1.log:", 0) == 0);  // -H, and just the filename
}

TEST(RunGrep_HandlesMissingTrailingNewline) {
    Scratch scratch;
    REQUIRE(scratch.ok(), "could not make a scratch directory");

    // A log that was cut off mid-write -- which is how a log looks when the
    // machine holding it dies. The last line still counts.
    const std::string path = scratch.WriteFile("machine.2.log",
        "needle one\n"
        "needle two, no trailing newline");

    std::string output;
    bool ran = false;
    const mp1::GrepResult result = Grep({"needle"}, path, &output, &ran);
    CHECK(ran);
    CHECK_EQ(result.line_count, uint64_t{2});
}

TEST(RunGrep_NoMatchIsExitOneNotAnError) {
    Scratch scratch;
    REQUIRE(scratch.ok(), "could not make a scratch directory");
    const std::string path = scratch.WriteFile("machine.3.log", "nothing to see\n");

    // The single most common way to lose points here: exit 1 means "no lines
    // matched", which is a correct answer. Treating it as a failure makes a
    // legitimately empty result look like a dead machine.
    std::string output;
    bool ran = false;
    const mp1::GrepResult result = Grep({"needle"}, path, &output, &ran);
    CHECK(ran);
    CHECK_EQ(result.exit_code, 1);
    CHECK_EQ(result.line_count, uint64_t{0});
    CHECK(output.empty());
}

TEST(RunGrep_BadRegexIsExitTwo) {
    Scratch scratch;
    REQUIRE(scratch.ok(), "could not make a scratch directory");
    const std::string path = scratch.WriteFile("machine.4.log", "anything\n");

    // grep ran perfectly well and disliked the pattern, so RunGrep still
    // succeeds -- only its exit code says no. grep's explanation goes to this
    // process's stderr, which is where the daemon's own log would get it; the
    // line you may see below is that, and it is meant to be there.
    std::string output;
    bool ran = false;
    const mp1::GrepResult result = Grep({"-E", "("}, path, &output, &ran);
    CHECK(ran);
    CHECK_EQ(result.exit_code, 2);
}

TEST(RunGrep_MissingLogFileIsExitTwo) {
    Scratch scratch;
    REQUIRE(scratch.ok(), "could not make a scratch directory");

    // On the VMs this is "somebody rebooted and the logs were never
    // regenerated". It has to look like an error, not like an empty result.
    std::string output;
    bool ran = false;
    const mp1::GrepResult result =
        Grep({"needle"}, scratch.dir() + "/machine.404.log", &output, &ran);
    CHECK(ran);
    CHECK_EQ(result.exit_code, 2);
    CHECK_EQ(result.line_count, uint64_t{0});
}

TEST(RunGrep_LargeOutputStreamsWithoutStalling) {
    Scratch scratch;
    REQUIRE(scratch.ok(), "could not make a scratch directory");

    // A pipe holds about 64 KB. grep blocks on write() once it is full, so a
    // parent that waits for the child before draining deadlocks forever on a
    // result this size. The suite's watchdog turns that into a failure instead
    // of a hung run -- see test_framework.hpp.
    const std::string line(99, 'x');
    std::string block;
    block.reserve(1u << 20);
    while (block.size() < (1u << 20)) block += "MATCHME " + line + "\n";

    const std::string path = scratch.Path("machine.5.log");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        for (int i = 0; i < 50; ++i) {  // ~50 MB
            out.write(block.data(), static_cast<std::streamsize>(block.size()));
        }
    }

    // Counted rather than collected: holding 50 MB of matches in the test as
    // well as streaming it proves nothing extra and doubles the memory.
    uint64_t streamed_bytes = 0;
    mp1::GrepResult result;
    std::string err;
    const bool ran = mp1::RunGrep({"MATCHME"}, path,
                                  [&streamed_bytes](const std::string& chunk) {
                                      streamed_bytes += chunk.size();
                                      return true;
                                  },
                                  &result, &err);
    REQUIRE(ran, err);
    CHECK_EQ(result.exit_code, 0);
    CHECK(streamed_bytes > (40u << 20));
    // Every line matched, so the count is the file's line count.
    const uint64_t lines_per_block = block.size() / (line.size() + 9);
    CHECK_EQ(result.line_count, lines_per_block * 50);
}

TEST(RunGrep_PassesThroughGrepFlags) {
    Scratch scratch;
    REQUIRE(scratch.ok(), "could not make a scratch directory");

    const std::string path = scratch.WriteFile("machine.6.log",
        "alpha beta\n"    // 1
        "BETA gamma\n"    // 2
        "delta\n"         // 3
        "alphabet\n"      // 4
        "a.b\n"           // 5
        "axb\n");         // 6

    // The spec calls out "all original grep options, especially arbitrary
    // regexes via -E", so this covers a flag of each shape: case folding,
    // inversion, word and line anchoring, numbering, counting, fixed strings,
    // and -E with alternation, a character class, anchors and a quantifier.
    CHECK_EQ(GrepLines({"-i", "beta"}, path), uint64_t{2});
    CHECK_EQ(GrepLines({"-w", "beta"}, path), uint64_t{1});
    CHECK_EQ(GrepLines({"-v", "beta"}, path), uint64_t{5});
    CHECK_EQ(GrepLines({"-x", "delta"}, path), uint64_t{1});
    CHECK_EQ(GrepLines({"-n", "alpha"}, path), uint64_t{2});
    CHECK_EQ(GrepLines({"-F", "a.b"}, path), uint64_t{1});   // literal dot
    // As a basic regex the dot is a wildcard, so this also picks up "axb" and
    // the "a b" inside "alpha beta" -- which is precisely why -F exists.
    CHECK_EQ(GrepLines({"a.b"}, path), uint64_t{3});
    CHECK_EQ(GrepLines({"-E", "(alpha|delta)"}, path), uint64_t{3});
    CHECK_EQ(GrepLines({"-E", "^a[lx]"}, path), uint64_t{3});
    CHECK_EQ(GrepLines({"-E", "be+ta"}, path), uint64_t{1});
    CHECK_EQ(GrepLines({"-E", "^delta$"}, path), uint64_t{1});

    // -c makes grep print one count line instead of the matches, and the spec
    // explicitly allows that as the answer. One printed line is one line.
    std::string output;
    bool ran = false;
    const mp1::GrepResult counted = Grep({"-c", "alpha"}, path, &output, &ran);
    CHECK(ran);
    CHECK_EQ(counted.line_count, uint64_t{1});
    CHECK(output.find("machine.6.log:2") != std::string::npos);
}

TEST(RunGrep_DoesNotInvokeAShell) {
    Scratch scratch;
    REQUIRE(scratch.ok(), "could not make a scratch directory");
    const std::string path = scratch.WriteFile("machine.7.log", "harmless line\n");

    // The pattern arrives from another machine over the network. If it ever
    // reached a shell, these would run as commands on this VM; because argv is
    // an array handed straight to execvp, they are just text that matches
    // nothing. The proof is that the files do not appear.
    const std::string marker_a = scratch.Path("pwned_dollar");
    const std::string marker_b = scratch.Path("pwned_backtick");
    CHECK_EQ(::access(marker_a.c_str(), F_OK), -1);

    std::string output;
    bool ran = false;
    mp1::GrepResult result = Grep({"$(touch " + marker_a + ")"}, path, &output, &ran);
    CHECK(ran);
    CHECK_EQ(result.exit_code, 1);  // matched nothing, as a literal string

    result = Grep({"`touch " + marker_b + "`"}, path, &output, &ran);
    CHECK(ran);
    CHECK_EQ(result.exit_code, 1);

    CHECK_EQ(::access(marker_a.c_str(), F_OK), -1);
    CHECK_EQ(::access(marker_b.c_str(), F_OK), -1);
}

// --- log generator --------------------------------------------------------

TEST(GenerateLog_IsDeterministic) {
    Scratch scratch;
    REQUIRE(scratch.ok(), "could not make a scratch directory");

    mp1::LogSpec spec;
    spec.machine_id = 4;
    spec.line_count = 5000;
    spec.seed       = 99;
    spec.patterns   = mp1::DefaultPatterns();

    const std::string first  = scratch.Path("first.log");
    const std::string second = scratch.Path("second.log");

    mp1::ExpectedCounts expected_a, expected_b;
    std::string err;
    REQUIRE(mp1::GenerateLog(spec, first, expected_a, &err), err);
    REQUIRE(mp1::GenerateLog(spec, second, expected_b, &err), err);

    // Byte-identical, or the ground truth the whole distributed suite asserts
    // against is only true on the machine that happened to generate it.
    std::ifstream a(first, std::ios::binary), b(second, std::ios::binary);
    const std::string bytes_a((std::istreambuf_iterator<char>(a)),
                              std::istreambuf_iterator<char>());
    const std::string bytes_b((std::istreambuf_iterator<char>(b)),
                              std::istreambuf_iterator<char>());
    CHECK(!bytes_a.empty());
    CHECK_EQ(bytes_a.size(), bytes_b.size());
    CHECK(bytes_a == bytes_b);
    CHECK(expected_a.by_token == expected_b.by_token);
}

TEST(GenerateLog_FillerNeverContainsPlantedTokens) {
    Scratch scratch;
    REQUIRE(scratch.ok(), "could not make a scratch directory");

    // Nothing planted at all, so every one of these greps must come back empty.
    // If this test ever fails, every expected count in the distributed suite is
    // quietly too low and the failures show up somewhere far less obvious.
    mp1::LogSpec spec;
    spec.machine_id = 1;
    spec.line_count = 20000;
    spec.seed       = 7;

    const std::string path = scratch.Path("machine.1.log");
    mp1::ExpectedCounts expected;
    std::string err;
    REQUIRE(mp1::GenerateLog(spec, path, expected, &err), err);

    const std::vector<std::string> tokens = {
        mp1::kRareToken,   mp1::kSomewhatToken, mp1::kFrequentToken,
        mp1::kOneLogToken, mp1::kSomeLogsToken, mp1::kAbsentToken,
    };
    for (const std::string& token : tokens) {
        CHECK_EQ(GrepLines({"-F", token}, path), uint64_t{0});
    }
}

TEST(GenerateLog_ExpectedCountsMatchRealGrep) {
    Scratch scratch;
    REQUIRE(scratch.ok(), "could not make a scratch directory");

    // This is what validates the ORACLE. Without it the distributed tests only
    // prove that two pieces of our own code agree with each other; with it, the
    // ground truth is checked against the same grep that will answer the query.
    //
    // Machine 3 gets every default pattern (it is the one-log machine and it is
    // odd); machine 2 gets only the three that go everywhere.
    for (int machine_id : {2, 3}) {
        mp1::LogSpec spec;
        spec.machine_id = machine_id;
        spec.line_count = 40000;
        spec.seed       = 12345;
        spec.patterns   = mp1::DefaultPatterns();

        const std::string path = scratch.Path(mp1::LogFileName(machine_id));
        mp1::ExpectedCounts expected;
        std::string err;
        REQUIRE(mp1::GenerateLog(spec, path, expected, &err), err);

        for (const auto& token_entry : expected.by_token) {
            const std::string& token = token_entry.first;
            CHECK_EQ(GrepLines({"-F", token}, path),
                     expected.by_token.at(token).at(machine_id));
        }
        // ...and the frequencies really are what log_gen.hpp says they are.
        CHECK_EQ(expected.by_token.at(mp1::kFrequentToken).at(machine_id),
                 uint64_t{4000});
        CHECK_EQ(expected.by_token.at(mp1::kSomewhatToken).at(machine_id),
                 uint64_t{40});
    }
}

TEST(GenerateLog_HitsTargetSize) {
    Scratch scratch;
    REQUIRE(scratch.ok(), "could not make a scratch directory");

    // The report's configuration: 60 MB per machine. The line count has to be
    // decided before the first line is written -- that is what makes the
    // planted counts exact -- so the size is an estimate, and this is the test
    // that the estimate is a good one.
    const uint64_t target = 60000000;
    mp1::LogSpec spec;
    spec.machine_id   = 1;
    spec.target_bytes = target;
    spec.seed         = 42;
    spec.patterns     = mp1::DefaultPatterns();

    const std::string path = scratch.Path("machine.1.log");
    mp1::ExpectedCounts expected;
    std::string err;
    REQUIRE(mp1::GenerateLog(spec, path, expected, &err), err);

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const uint64_t size = static_cast<uint64_t>(file.tellg());
    const double error = std::abs(static_cast<double>(size) - target) / target;
    CHECK(error < 0.03);
    if (error >= 0.03) {
        std::printf("  wrote %llu bytes for a %llu byte target\n",
                    static_cast<unsigned long long>(size),
                    static_cast<unsigned long long>(target));
    }
}
