// log-query -- the distributed querier. Runs on ANY machine in the cluster.
//
//   log-query [--config config/machines.txt] [--timeout-ms 2000] [-- ] <grep args...>
//
// Everything after the client's own flags is passed through to grep untouched,
// so all of grep's options work:
//
//   log-query -c ERROR
//   log-query -E '(WARN|ERROR).*timeout'
//   log-query -i -n "connection refused"
//   log-query -v -E '^DEBUG'
//
// Pass-through parsing is fiddly and worth getting right: stop parsing YOUR
// flags at the first argument you do not recognize (or at a literal "--") and
// forward the entire rest verbatim. If you try to be clever you will eventually
// eat a flag that belonged to grep.
//
// Exit code convention, mirroring grep so log-query composes in shell
// pipelines:
//   0 = at least one machine matched
//   1 = no matches anywhere (and no failures)
//   2 = a grep error, or at least one machine was unreachable

#include <cstdio>
#include <string>
#include <vector>

#include "mp1/client.hpp"
#include "mp1/config.hpp"

namespace {

void Usage() {
    std::fprintf(stderr,
                 "usage: log-query [--config <path>] [--timeout-ms <n>] "
                 "[--counts-only] -- <grep args...>\n");
}

}  // namespace

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    Usage();
    // TODO:
    //   1. Parse log-query flags, collect the grep pass-through args.
    //   2. LoadMachines(config)
    //   3. summary = RunQuery(machines, grep_args, opts)
    //   4. Print the per-machine summary table, e.g.
    //
    //        machine.1.log   1423 lines
    //        machine.2.log      0 lines
    //        machine.3.log      -- UNREACHABLE
    //        ----------------------------------
    //        total           1423 lines from 2/3 machines   (412 ms)
    //
    //      The per-file count with its filename is a hard spec requirement, and
    //      showing UNREACHABLE separately from a zero count is what proves your
    //      fault tolerance actually works.
    //   5. Return the exit code described above.
    return 2;
}
