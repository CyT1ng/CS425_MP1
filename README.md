# CS425 MP1 — Distributed Log Querier

Run a `grep` across log files on every machine in the cluster, from any machine
in the cluster, and get back per-file line counts with the filename each match
came from — tolerating machines that have failed.

**Group:** *(names + NetIDs)*

---

## Build

    make

Requires a C++17 compiler (`g++` or `clang++`) and GNU make. No external
dependencies — the CS VM Cluster toolchain is enough as-is.

Produces four binaries in `bin/`:

| Binary | Role |
|---|---|
| `mp1d` | log server daemon; one per machine |
| `dgrep` | the distributed querier; run from any machine |
| `mp1gen` | deterministic log generator |
| `mp1tests` | the full unit test suite |

## Run the unit tests

    make test          # or: ./scripts/run_tests.sh

Runs the local tests and the distributed tests. The distributed tests bring up
their own throwaway cluster of `mp1d` processes on `127.0.0.1`, generate logs,
query them, and inject failures — no manual setup, and no VMs required. Exit
status is nonzero if anything fails.

## Run MP1

**1. Configure the cluster.** `config/machines.txt` is the VM cluster — one
`id host port` line per machine; regenerate it with
`./scripts/gen_machines.sh <gid> 10 <port> > config/machines.txt`. Ship the same
file to every machine so any of them can query. `config/local.txt` is the
several-processes-on-one-host config for development; pass it with
`--config`. Keep them in separate files — duplicate ids in one file are
rejected at startup.

**2. Start a daemon on every machine.**

    ./bin/mp1gen --id <i> --seed 42 --lines 300000    # generate machine.<i>.log
    ./bin/mp1d   --id <i> --port 4425                 # serve it

On the VM cluster, `./scripts/deploy.sh` does the clone, build, log generation,
and daemon start across every machine in the config at once:

    MP1_REPO=git@github.com:<org>/<repo>.git ./scripts/deploy.sh

Locally, `./scripts/start_cluster.sh 6` brings up a 6-process cluster on one
host for development.

**3. Query from any machine.**

    ./bin/dgrep -- ERROR
    ./bin/dgrep -- -c ERROR
    ./bin/dgrep -- -E '(WARN|ERROR).*timeout'
    ./bin/dgrep -- -i -n "connection refused"
    ./bin/dgrep -- -v -E '^DEBUG'

Everything after `--` is passed to `grep` untouched, so every grep option works,
including arbitrary `-E` regexes. Output looks like:

    machine.1.log:2026-09-13T04:12:01 ERROR db connection refused
    ...
    machine.1.log    1423 lines
    machine.2.log       0 lines
    machine.3.log      -- UNREACHABLE
    ------------------------------------
    total            1423 lines from 2/3 machines   (412 ms)

Exit status mirrors grep: `0` matched, `1` no matches anywhere, `2` an error or
an unreachable machine.

## Reproducing the report numbers

    ./scripts/measure.sh config/machines.txt 7
    python3 report/plot_latency.py report/data/latency.csv

4 machines × 60 MB logs, 7 trials per query class, mean and standard deviation
plotted together with SD as error bars.

## Layout

    include/mp1/   headers — the design lives in these comments
    src/           implementation + the three binaries' main()
    tests/         test framework, cluster harness, local + distributed tests
    scripts/       cluster startup, VM deploy, test runner, measurements
    config/        machines.txt (VM cluster), local.txt (one-host dev)
    report/        plotting script and measurement data

## Design summary

`dgrep` ships the **query to the data**, never the data to the querier: each
machine greps its own log locally and returns only matching lines. At the demo's
scale, fetching logs would move 60 MB per machine across the network before any
matching starts, while local grep reads the same 60 MB at memory bandwidth on
all machines *concurrently*. This is a scatter/gather fan-out — no partitioning,
no shuffle, no reduce phase, and explicitly not MapReduce.

The querier launches one `std::async` task per machine, each with its own
connect deadline, so a failed machine costs one timeout instead of stalling the
query. Each task returns a finished result by value, which means no shared state
and no locks. Every machine is reported explicitly — a line count, or
`UNREACHABLE`/`PARTIAL` — so "no matches" is never confused with "machine down".

### Wire protocol

Deliberately text, not binary. Headers are ASCII lines; payloads carry their
length so the reader always knows where they end:

    client -> server    ARGS <argc>\n  then argc x  <len>\n<bytes>
    server -> client    D <len>\n<bytes>   ...zero or more
                        E <exit_code> <line_count>\n   ends the stream

That costs a few bytes against a binary encoding and buys no byte-order code, no
bit shifting, and the ability to point `nc` at a daemon and read the exchange.

The `E` line is what makes failure detectable. Without it, "grep matched
nothing", "grep rejected the regex", and "the machine died" are the same empty
stream. `exit_code` separates the first two; `line_count`, checked against the
lines actually received, catches a peer that died halfway.

The request carries no filename — the server appends its own. So each machine
necessarily greps `machine.<i>.log`, and a querier cannot ask a peer for an
arbitrary file, because the protocol has no field to put one in.

### Staged on purpose

Some hardening is deliberately deferred and tracked in `MP1_PLAN.md` Phase D: a
version greeting, length caps on incoming frames, and a second pipe carrying
grep's stderr back to the querier. Each is listed with its reason at the bottom
of `include/mp1/protocol.hpp`.

See `include/mp1/protocol.hpp` for the wire format, `include/mp1/net.hpp` for the
`Conn` socket wrapper, and `include/mp1/client.hpp` for the fan-out and
fault-tolerance contract.
