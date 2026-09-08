# CS425 MP1 — Distributed Log Querier

Run a `grep` across log files on every machine in a cluster, from any machine in
the cluster, and get back the matching lines labelled with which machine each
one came from — while machines that are down are reported rather than silently
skipped.

**Group 91** — *(names + NetIDs)*

---

## The whole system is two files

    server.cpp   one machine's daemon: holds one log, answers questions about it
    query.cpp    the querier: asks every machine at once, collects the answers

No libraries, no headers of our own. Both are meant to be read top to bottom.

## Build

    make

Needs a C++17 compiler and GNU make — nothing else. Produces `server` and
`query`.

## Try it on this computer first

    ./run_local.sh 4 10000        # 4 "machines" as separate processes
    ./query local.txt COMMON_TOKEN

Each machine is its own process with its own log file and port, so as far as the
code is concerned that is a real distributed system — real sockets, real
fan-out, real failures — with no VMs needed.

The log generator plants tokens on a fixed schedule, so **every answer is
predictable in advance**:

| Query | Matches per machine |
|---|---|
| `COMMON_TOKEN` | lines / 10 |
| `RARE_TOKEN` | lines / 1000 |
| `-c ERROR` | lines / 4 |
| `ONLY_ON_THREE` | 0, except on machine 3 |

With 4 machines × 10,000 lines, `COMMON_TOKEN` must return exactly 4,000 lines.

Kill one and ask again — the others still answer, and the dead one is named:

    kill -9 $(sed -n 2p cluster/pids)
    ./query local.txt RARE_TOKEN

## Run it on the VM cluster

`machines.txt` lists the ten VMs. From your laptop, or from any one VM:

    MP1_REPO=https://github.com/CyT1ng/CS425_MP1.git ./deploy.sh

That runs **once**, from **one** machine, and ssh's out to all ten — clone,
build, generate a log, start the daemon, in parallel. Then query from any of
them:

    ssh fa26-cs425-9103.cs.illinois.edu
    cd mp1 && ./query machines.txt -c ERROR

The VMs have no persistent storage, so after a reboot just run `./deploy.sh`
again. That is the whole recovery procedure.

## Queries

Everything after the machine list is passed straight to `grep`, so every grep
option works:

    ./query machines.txt ERROR
    ./query machines.txt -c ERROR
    ./query machines.txt -i "connection refused"
    ./query machines.txt -E "request (100|200) handled"
    ./query machines.txt -v -E "^2026"

Output: the matching lines, each prefixed with the log it came from, then a
per-machine summary.

    machine.1.log      1000 lines   (8 ms)
    machine.2.log        -- unreachable   (0 ms)
    machine.3.log      1000 lines   (8 ms)
    ---------------------------------------------
    total              2000 lines from 2/3 machines   (8 ms)

Exit status follows grep: `0` matched, `1` matched nothing anywhere, `2` a
machine failed.

---

## How it works

**Ship the query to the data.** Each machine greps its own log locally and sends
back only the matching lines. Copying ten 60 MB logs to one machine to search
them there would move 600 MB to find, sometimes, three lines — and would do the
searching on one CPU instead of ten.

**Ask every machine at the same time.** `query.cpp` starts one `std::async` task
per machine before waiting on any of them, so a query costs the time of the
*slowest* machine rather than the sum of all of them. It is also what keeps one
dead machine from holding up the other nine.

**The message format says when a machine is finished.**

    client -> server    one line per grep argument, then an empty line
    server -> client    D <n>\n + n bytes     (0 or more times)
                        E <grep's exit code>  (exactly once)

The `E` line is the part that looks unnecessary and is not: without it, "grep
found nothing" and "this machine died" are identical on the network — both are a
closed connection with nothing in it.

**A failed machine is a normal outcome.** `query_one()` never fails upward; a
dead machine comes back as a result with `answered = false` and gets a word in
the summary table where its count would be. Printing `0`, or leaving the row
out, would look exactly like a machine that was fine and had no matches.

**Connecting has a timeout.** A machine that is switched off never answers at
all, and an ordinary `connect()` waits minutes. `connect_with_timeout()` uses a
non-blocking socket plus `poll()`, so a dead machine costs two seconds — once,
for all the dead machines together, because they wait concurrently.

**The server never trusts the client with a filename.** It builds
`machine.<its own id>.log` itself, so machine 3 always searches machine 3's log.

**grep is the real grep**, started with `fork` + `execvp` and an argument array —
never a shell, which would re-interpret the pattern and would hand a stranger on
the network a shell on the VM.

---

## Files

    server.cpp     the daemon
    query.cpp      the querier
    gen_logs.sh    writes a log with predictable contents (awk, no randomness)
    run_local.sh   starts a pretend cluster on this computer
    deploy.sh      builds and starts the daemon on all ten VMs
    machines.txt   the ten VMs
    Makefile

An earlier, much larger version of this project — layered into modules, with a
43-case unit test suite and latency measurement tooling — is preserved in git:

    git checkout full-version
