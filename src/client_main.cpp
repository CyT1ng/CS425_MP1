// log-query -- the distributed querier. Runs on ANY machine in the cluster.
//
//   log-query [--config config/machines.txt] [--timeout-ms 2000] [--] <grep args...>
//
// Everything after log-query's own flags is passed through to grep untouched,
// so all of grep's options work:
//
//   log-query -c ERROR
//   log-query -E '(WARN|ERROR).*timeout'
//   log-query -i -n "connection refused"
//   log-query -v -E '^DEBUG'
//
// Pass-through parsing is fiddly and worth getting right: this stops parsing
// its OWN flags at the first argument it does not recognize (or at a literal
// "--") and forwards the entire rest verbatim. Anything cleverer eventually
// eats a flag that belonged to grep.
//
// Exit code convention, mirroring grep so log-query composes in shell
// pipelines:
//   0 = at least one machine matched
//   1 = no matches anywhere (and no failures)
//   2 = a grep error, or at least one machine was unreachable

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "mp1/client.hpp"
#include "mp1/config.hpp"
#include "mp1/net.hpp"

namespace {

void Usage() {
    std::fprintf(stderr,
                 "usage: log-query [--config <path>] [--timeout-ms <n>] "
                 "[--counts-only] -- <grep args...>\n");
}

// A numeric flag value, or a clear death. See the note in server_main.cpp: a
// timeout that silently parses as 0 would make every machine unreachable.
long long RequireNumber(const char* flag, const char* value,
                        long long low, long long high) {
    const std::string text = value ? value : "";
    const bool numeric = !text.empty() && text.size() <= 18 &&
                         text.find_first_not_of("0123456789") == std::string::npos;
    const long long number = numeric ? std::stoll(text) : 0;
    if (!numeric || number < low || number > high) {
        std::fprintf(stderr, "log-query: %s expects a number in %lld..%lld, got \"%s\"\n",
                     flag, low, high, text.c_str());
        std::exit(2);
    }
    return number;
}

}  // namespace

int main(int argc, char** argv) {
    // A machine can die while we are reading its answer; without this, its
    // socket closing would kill the querier instead of failing one machine.
    mp1::IgnoreSigpipe();

    std::string        config_path = "config/machines.txt";
    mp1::QueryOptions  opts;

    int i = 1;
    for (; i < argc; ++i) {
        const std::string flag  = argv[i];
        const char*       value = (i + 1 < argc) ? argv[i + 1] : nullptr;

        if (flag == "--") {          // explicit end of our flags
            ++i;
            break;
        } else if (flag == "--config") {
            if (!value) {
                std::fprintf(stderr, "log-query: --config expects a path\n");
                return 2;
            }
            config_path = value;
            ++i;
        } else if (flag == "--timeout-ms") {
            opts.connect_timeout =
                std::chrono::milliseconds(RequireNumber("--timeout-ms", value, 1, 600000));
            ++i;
        } else if (flag == "--read-timeout-ms") {
            opts.read_timeout =
                std::chrono::milliseconds(RequireNumber("--read-timeout-ms", value, 1, 3600000));
            ++i;
        } else if (flag == "--counts-only") {
            opts.counts_only = true;
        } else if (flag == "--help") {
            // Only the long form is claimed: "-h" is grep's own flag for
            // suppressing filenames, and swallowing it here would break a
            // legitimate query.
            Usage();
            return 2;
        } else {
            break;  // not ours -- everything from here belongs to grep
        }
    }

    const std::vector<std::string> grep_args(argv + i, argv + argc);
    if (grep_args.empty()) {
        std::fprintf(stderr, "log-query: no grep arguments given\n");
        Usage();
        return 2;
    }

    // All or nothing: one bad line in the config means no query at all, rather
    // than a query that quietly covers nine machines out of ten.
    std::vector<mp1::Machine> machines;
    std::string err;
    if (!mp1::LoadMachines(config_path, machines, &err)) {
        std::fprintf(stderr, "log-query: %s\n", err.c_str());
        return 2;
    }

    const mp1::QuerySummary summary = mp1::RunQuery(machines, grep_args, opts);
    mp1::PrintSummary(summary, opts);
    return mp1::QueryExitCode(summary);
}
