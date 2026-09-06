# MP1 — Hands-On Build Guide

Companion to `MP1_PLAN.md` (what to build) and `MP1_REQUIREMENTS.md` (what is
graded). This file is **the order to build it in**, with a way to check each
step actually works before moving on.

**Code due Sun Sep 13, 11:59 PM. Demo Mon Sep 14.**

---

## The one rule

**Never spend a day without something you can run.**

The order below is built around vertical slices: after each milestone there is a
command you can type that visibly does something new. If you build all the
plumbing first and only connect it on day 6, every bug arrives at once and you
cannot tell which layer is wrong.

---

## Day 0 — baseline (20 minutes, do it now)

    make clean && make all      # must succeed
    make test                   # must run; tests are empty, that is fine
    git add -A && git commit -m "skeleton builds clean" && git push

**Done when:** four binaries in `bin/`, and a green build committed. If anything
here fails, fix it before writing a line of logic.

---

## Day 1 — `Conn`, the socket wrapper

Everything else stands on this. `include/mp1/net.hpp` has the contract.

**Write, in this order:**

1. `Conn::WriteAll` — loop until every byte is out. Easiest one; start here to
   get familiar with the shape.
2. `Conn::ReadLine` — the important one. Keep a `std::string buf_`. Look for
   `\n` in it; if it is not there, `read()` more and append. When you find one,
   return everything before it and **erase it plus the newline** from `buf_`.
3. `Conn::ReadExactly` — take what `buf_` already holds, then `read()` until you
   have `n` bytes. `read()` returning 0 before that is the peer dying: fail.
4. `Conn::SetReadTimeout` — `setsockopt(SO_RCVTIMEO)`.

**The bug you will hit:** forgetting that leftover bytes after the newline
belong to the *next* message. Throw them away and the next read silently loses
data.

**Verify:** fill in `Conn_ReadLineHandlesSplitAndCoalescedReads` and
`Conn_ReadExactlyDetectsShortStream` in `tests/test_unit_local.cpp`. Use
`socketpair()` — no ports, no daemons, runs in milliseconds.

    make test

**Done when:** both `Conn_*` tests pass.

---

## Day 1 (cont.) — listening and connecting

**Write:** `Listen`, `Accept`, `IgnoreSigpipe`, then `Connect`.

`Connect` is the hard one and it is worth doing carefully: non-blocking socket,
`poll(POLLOUT)`, then check `SO_ERROR`. Linux ignores socket timeouts on
`connect()`, so this is the only way the timeout is real — and without a real
timeout, one dead VM stalls your entire query for minutes.

**Verify by hand:**

    ./bin/mp1d --id 1 --port 4425      # will not work yet, but should not crash
    nc -l 4425                          # in one terminal
    # then connect from another and see bytes arrive

**Milestone:** two processes on your laptop can exchange bytes through your own
socket code.

---

## Day 2 — the protocol

`include/mp1/protocol.hpp` has the exact format. All five functions are short
because `Conn` does the looping.

**Write:** `SendRequest` → `RecvRequest` → `SendData` → `SendEnd` → `RecvFrame`.

Write send and receive as a **pair** and test the round trip immediately. Do not
write all five and then start testing.

**Verify:** the four `Protocol_*` tests, all over `socketpair()`.

Pay attention to `Protocol_RequestRoundTrips` — include an argument containing a
literal newline. That case is the entire reason arguments are length-prefixed
instead of one per line, and a naive implementation splits it.

**Done when:** all six `Conn_*`/`Protocol_*` tests pass. **This is the end of
Phase A.** Commit.

---

## Day 3 — one machine, end to end

The most satisfying day. Three pieces, then a real query.

### 3a. `RunGrep`

`pipe()`, `fork()`, `execvp("grep", ...)` with argv
`{ "grep", "-H", user_args..., log_path }`.

Two mistakes that cost hours:

- **Close the pipe's write end in the parent, first.** If you do not, the pipe
  never reports EOF, because you are still holding it open yourself, and your
  read loop hangs forever.
- **Read before `waitpid`.** A pipe holds ~64 KB. On a big result grep blocks
  writing, you block waiting for it to exit, and nothing ever moves.

Count `\n` in each chunk as you stream it. Do not run grep twice to get a count.

**Verify:** the `RunGrep_*` tests. Get `RunGrep_NoMatchIsExitOneNotAnError`
right — exit 1 is a *successful* answer, and conflating it with failure is the
single most common way to lose points here.

### 3b. `mp1gen` and `GenerateLog`

Seed a `std::mt19937_64` with `spec.seed`. Never `rand()`, never the clock, or
your logs differ between machines and every expected count becomes fiction.

Filler vocabulary must be **disjoint** from your planted tokens. Assert it.

**Verify:** `GenerateLog_IsDeterministic` — same seed twice, byte-identical
files. And `GenerateLog_ExpectedCountsMatchRealGrep`, which checks your oracle
against actual grep. Without that one, your distributed tests only prove two
pieces of your own code agree with each other.

### 3c. `mp1d`

Parse `--id`, `--port`, `--log-dir`. `IgnoreSigpipe()`. `Listen`. Then an accept
loop that hands each connection to a detached thread:

    RecvRequest -> RunGrep(args, log_dir + "/" + LogFileName(id), sink) -> SendEnd

where `sink` calls `SendData`. On any failure, still `SendEnd` with exit 2 — the
client needs *something* to distinguish a broken machine from a silent one.

**Milestone — do this by hand:**

    ./bin/mp1gen --id 1 --seed 42 --lines 50000 --out-dir /tmp/mp1
    ./bin/mp1d --id 1 --port 9401 --log-dir /tmp/mp1 &
    nc 127.0.0.1 9401
    ARGS 2
    2
    -c
    5
    ERROR

You should see `D`/`E` lines come back. **This is the payoff of a text
protocol** — you just used the whole server without writing a client.

---

## Day 4 — the querier

### 4a. `QueryOne`, against one machine

`Connect` → `SendRequest` → `RecvFrame` until the END frame. Fill in a
`MachineResult`.

**This function must never throw or propagate a failure.** A dead machine is a
normal return value with `status = kUnreachable`. That is the entire fault
tolerance requirement, expressed as a return type.

### 4b. `client_main.cpp`

Parse `dgrep` flags, then `LoadMachines`, then `RunQuery`, then `PrintSummary`,
then the exit code (0 matched / 1 no match / 2 error).

Flag parsing rule: stop parsing **your** flags at the first argument you do not
recognise, or at a literal `--`, and forward the entire rest verbatim. Get
clever and you will eventually eat a flag that belonged to grep.

Print errors to **stderr**, not stdout — `measure.sh` pipes stdout.

### 4c. `RunQuery` — the fan-out

One `std::async(std::launch::async, QueryOne, ...)` per machine, then collect
every future. `t0` before, `t1` after.

**`std::launch::async` is not optional.** Without it the task may be deferred
until you call `get()`, which silently makes your fan-out sequential. Your code
still works; your latency plot is quietly wrong.

**Milestone:**

    ./scripts/start_cluster.sh 6
    ./bin/dgrep --config config/local.txt -- -c ERROR

Six machines answering one query. **This is the MP working.** Commit.

---

## Day 5 — tests and failure

### 5a. `Cluster` harness

`fork` + `exec` N daemons on `base_port + i`, each with its own log dir.

**Poll for readiness — never `sleep()` and hope.** Try connecting in a loop
until it works. Fixed sleeps are how a suite becomes flaky on a loaded VM.

`StopAll` must run even when a test fails, or a rerun collides with orphaned
daemons still holding the ports. Pick a `base_port` away from 9400 so tests do
not collide with a `start_cluster.sh` you left running.

### 5b. Distributed tests

Every named test in `tests/test_distributed.cpp`. Cover both axes the spec
requires: rare / somewhat frequent / frequent, and one log / some logs / all
logs / no log.

### 5c. Fault tolerance

- `Cluster::Kill` uses **SIGKILL**, not SIGTERM. A clean shutdown closes the
  socket politely and exercises a different path than a VM that vanishes.
- Test the slow-failure case against a **blackhole IP**, not localhost.
  Localhost refuses instantly and proves nothing about your timeout.
- Killed mid-stream → `kPartial`, caught by comparing the END line's count
  against the lines actually received.

**Done when:** the whole suite passes 10 runs in a row without a flake.

---

## Day 6 — the cluster, and hardening

Get on the VMs early. Everything is slower there and you cannot debug a deploy
script the night before.

    MP1_REPO=<your course repo> ./scripts/deploy.sh

Remember: **the VMs have no persistent storage.** After a reboot the clone, the
binaries and the logs are all gone. Rehearse recovering from that.

Then Phase D hardening from `MP1_PLAN.md`: version greeting, length caps, and
grep's stderr carried back. Each is listed with its reason at the bottom of
`include/mp1/protocol.hpp`.

---

## Day 7 — measurements

    ./scripts/measure.sh config/machines.txt 7
    python3 report/plot_latency.py report/data/latency.csv

4 machines × 60 MB logs, 7 trials per class, warm-up discarded.

**Mean and SD on the same plot, SD as error bars.** `MP1_REQUIREMENTS.md` calls
this out as a common source of lost points.

---

## Day 8 — report and submit

Under one page, 12 pt. One point lost per line over.

The discussion writes itself if you notice this: grep reads all 60 MB no matter
how many lines match, so the *search* cost is nearly identical across all three
query classes. What changes is the size of the result set. So the frequent query
is dominated by network transfer, and rare and infrequent look nearly identical.

Then: Gradescope with **pages tagged** and partner tagged, code submission form,
README accurate.

---

## If you fall behind

Cut in this order, and say so in the report rather than hiding it:

1. Phase D hardening (keep the length caps if you can)
2. The breadth of the distributed test matrix — keep at least one test per axis
3. Number of trials, down to the required 5

**Never cut:** the parallel fan-out, per-file counts with filenames, or
reporting failed machines distinctly. Those are the graded core.
