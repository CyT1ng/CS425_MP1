# MP1 — Architecture Tour

A guided walk through the distributed log querier, written to be **presented**.
Read it top to bottom once and you can explain the system; jump to a section
when someone asks about one piece.

**Two ways to use it**

| You are… | Read |
|---|---|
| introducing the project | §0 → §1 → §2. Thirty seconds, the map, then one query end to end. |
| being asked about one component | §3 — one section per file, each saying what it does, why it is built that way, and what breaks without it. |
| being challenged on a decision | §4 (the questions people actually ask) and §5 (every trade-off, stated as a trade-off). |
| asked "show me" | §7, the jump table into the code. |

---

## 0. The thirty-second version

Ten machines, each holding its own log file. Sit at **any** one of them, type a
`grep`, and get back the matching lines from **all ten** — each line labelled
with the machine it came from. If some machines are down you still get the
answers from the ones that are up, and the dead ones are named, not skipped.

The whole system turns on one decision:

> **Ship the query to the data, never the data to the query.**

The search runs *on each machine*, against its own local disk, all ten at once.
Only matching lines cross the network.

| | Fetch the logs to the querier | Grep locally (what this does) |
|---|---|---|
| Bytes on the wire | 60 MB × N, always | only the matches |
| Where matching happens | one CPU, serially | N machines, concurrently |
| Rare pattern | 600 MB moved to find ~10 lines | a few KB |
| Frequent pattern | 600 MB moved | ~24 MB |

The two approaches only converge if a pattern matches nearly every line. Local
grep never loses.

**It is not MapReduce**, which the spec forbids: there is no key space, no
partitioning, no shuffle, no reduce phase, and no framework re-executing failed
tasks. It is a scatter/gather fan-out RPC. Say exactly that.

---

## 1. The map

### 1.1 Three binaries

| Binary | What it is | Runs |
|---|---|---|
| **`log-server`** | the **daemon**. Owns one log file, answers questions about it. | always, on all 10 machines |
| **`log-query`** | the **querier**. Asks all ten daemons the same question at once. | whenever you type a query |
| `log-gen` | makes log files with known contents, for the tests and the measurements | at setup, and after every VM reboot |
| `run-tests` | the whole test suite | `make test` |

The important sentence: **every machine runs `log-server`, and every machine can
run `log-query`.** Client and server are *roles*, not machines. Machine 3's
querier talks to machine 3's own daemon through a socket, exactly the way it
talks to the other nine. Nothing about machine 1 is special, and there is a test
that proves it (`Distributed_AnyMachineCanBeTheQuerier`).

### 1.2 The layer stack

```
   log-query (client_main.cpp)          log-server (server_main.cpp)
   flags, config, exit code             accept loop, one thread per connection
          |                                        |
   client.cpp -- the fan-out            grep_runner.cpp -- runs the real grep
   one task per machine                 pipe + fork + execvp
          |                                        |
          +--------------------+-------------------+
                               |
                     protocol.cpp -- bytes <-> messages
                               |
                     net.cpp -- Conn: the socket, and the
                                looping that TCP forces on you
                               |
                        the operating system

   config.cpp   -- who the machines are
   log_gen.cpp  -- test data, and the ground truth for it
```

Each layer talks only to the layer below it. That is what makes each one
testable on its own: `net.cpp` and `protocol.cpp` are tested over a
`socketpair()` with no ports and no daemons at all, and `grep_runner.cpp` is
tested against real files with no network.

---

## 2. The tour: one query, end to end

You type:

```
./bin/log-query -- -c ERROR
```

Ten stops. Each one ends with the question people usually ask there.

### Stop 1 — `log-query` splits the command line

It parses its own flags (`--config`, `--timeout-ms`, `--counts-only`) and stops
at the first argument it does not recognise, or at a literal `--`. Everything
from there on is grep's, forwarded byte for byte.

> **If they ask** *"why not parse grep's flags too?"* — because grep has dozens
> and gains more, and any flag we fail to model is a query we silently break.
> Note that `log-query` deliberately does **not** claim `-h`: that is grep's
> flag for suppressing filenames, and swallowing it would break a real query.

### Stop 2 — it reads the machine list

`LoadMachines` turns `config/machines.txt` into a list of `Machine{id, host,
port}`. The spec explicitly permits hard state, so membership is a file you
write, not something discovered on the network.

It is **all or nothing**: one bad line yields zero machines and an error naming
the file, the line number and the offending value — never nine machines and a
shrug.

> **If they ask** *"why so strict?"* — because the alternative failure is
> silent. A cluster that quietly queries nine of ten machines returns a wrong
> answer that looks exactly like a right one.

### Stop 3 — it launches every machine at once

This is the single most important line in the project. `RunQuery` starts one
`std::async(std::launch::async, QueryOne, …)` per machine **before waiting on
any of them**, then collects the futures in config order.

- Sequentially, the query costs the **sum** of ten machines' times.
- Concurrently, it costs the time of the **slowest one**.

The clock starts before the launches and stops after the last future returns,
which is precisely the spec's definition: *time for the last machine to respond
with the last byte*.

> **If they ask** *"is `std::async` really concurrent?"* — only with
> `std::launch::async`. The default policy lets the implementation defer the
> task until `get()`, which would silently serialise the fan-out: the code still
> works, and the latency plot is quietly wrong.
> `FaultTolerance_DeadMachineDoesNotDelayLiveOnes` fails if anyone ever changes
> it.

### Stop 4 — each task connects, with a real timeout

`Connect` resolves the hostname (IPv4 or IPv6) and tries each address in turn
under one shared deadline.

A machine can be dead in two ways, and they look very different:

| Failure | What the OS does | What we report |
|---|---|---|
| VM up, daemon not running | refuses instantly (`ECONNREFUSED`) | `UNREACHABLE`, in milliseconds |
| VM gone entirely | nothing answers at all | `UNREACHABLE`, after the timeout |

> **If they ask** *"why not just set a socket timeout?"* — because Linux ignores
> `SO_RCVTIMEO` for `connect()`. The timeout only becomes real with a
> non-blocking socket, `poll(POLLOUT)`, and then a check of `SO_ERROR` to tell
> "connected" from "refused". Without that, one dead VM stalls the entire query
> for the OS default, which is **minutes**.

### Stop 5 — it sends the grep arguments, and nothing else

`SendRequest` writes:

```
ARGS 2
2
-c
5
ERROR
```

`ARGS <argc>`, then each argument as `<length>\n<bytes>\n`. **There is no
filename field in the request.** The server appends its own.

> **If they ask** *"why length-prefix the arguments instead of one per line?"* —
> because a grep pattern can contain a newline. The length is what delimits;
> the newline after the bytes is a terminator that makes the request typeable
> straight into `nc` and proves the length was honest (see §3.3).

### Stop 6 — the daemon accepts, on its own thread

`log-server` runs `IgnoreSigpipe()`, listens, and hands every connection to a
detached thread. A malformed request ends that one connection and nothing else:
the thread body is wrapped in a `try`/`catch`, because an exception escaping a
thread calls `std::terminate` and would take the whole daemon down.

> **If they ask** *"why a thread per connection when each querier only sends one
> request?"* — because several people query at once. Both partners at the demo,
> and the test suite deliberately. A single-threaded accept loop serialises them
> and looks like a hang.

### Stop 7 — the daemon adds its own filename

`RunGrep` builds the argument vector as an **array**:

```
{ "grep", "-H", <the user's arguments…>, "machine.3.log" }
```

The filename comes from this machine's own `--id`, never off the wire. Machine 3
searches `machine.3.log`; machine 7 searches `machine.7.log`.

> **If they ask** *"could I make a peer grep `/etc/passwd`?"* — the *request*
> has nowhere to put a path, so the daemon always searches its own log. Be
> precise about the limit of that claim, though: the pass-through arguments are
> still grep's, and grep accepts extra file operands, so a caller could name a
> second file of their own. The guarantee is "this machine always searches its
> own log", not "grep is sandboxed". Everything runs as the same user on a
> trusted class cluster; a stricter version would need a grep option parser,
> which would break legitimate queries.

### Stop 8 — it runs the *real* grep and streams the output back

`fork` + `execvp`, one pipe for stdout. The parent closes the write end
**first**, drains the pipe counting newlines as it goes, and calls `waitpid`
**last**. Each chunk is wrapped in a `D <len>` frame and written to the socket
as it appears, so a 24 MB answer never has to fit in the daemon's memory.

> **If they ask** *"why not `system("grep …")`?"* — a shell would re-interpret
> the pattern, so `-E '(foo|bar)+'` would break depending on how the user
> happened to quote it, and it would hand a remote caller a shell on the VM.
> `RunGrep_DoesNotInvokeAShell` proves the difference by sending
> `$(touch …)` as a pattern and checking the file never appears.

> **If they ask** *"why drain before `waitpid`?"* — a pipe holds about 64 KB. On
> a big result grep blocks writing, we block waiting for it to exit, and nothing
> ever moves again. `RunGrep_LargeOutputStreamsWithoutStalling` runs ~50 MB
> through it, and the suite has a watchdog so a deadlock fails loudly instead of
> hanging the run.

### Stop 9 — the END line closes the stream

```
E <exit_code> <line_count>
```

Sent **always**, even when grep failed. This is the one frame that cannot be
simplified away. Without it, these three are byte-identical on the wire — all of
them are "the connection closed and there was nothing in it":

| Situation | What it really is |
|---|---|
| grep exited 1, nothing matched | **a correct answer** |
| grep exited 2, bad regex | an error |
| the machine was killed | a dead peer |

`exit_code` separates the first two. `line_count`, compared against the lines
that actually arrived, catches a machine that died halfway through sending. No
END at all means it vanished.

> **If they ask** *"what does grep's exit code 1 mean?"* — "no lines matched",
> which is a **successful** answer. Only 2 is an error. Conflating them is the
> most common way to lose points here, and
> `RunGrep_NoMatchIsExitOneNotAnError` exists for exactly that reason.

### Stop 10 — the querier prints the table

Matching lines first, then the per-machine summary, then one machine-readable
line:

```
machine.1.log:2026-09-13T04:12:01.375Z ERROR [db] connection refused ...
...
machine.1.log        1423 lines   (147 ms)
machine.2.log           0 lines   (139 ms)
machine.3.log          -- UNREACHABLE   (2001 ms)
----------------------------------------------------
total                1423 lines from 2/3 machines   (412 ms)
SUMMARY latency_ms=412 total_lines=1423 machines_ok=2 machines_failed=1
```

Results and the table go to **stdout**; the reason each machine failed goes to
**stderr**. Exit status mirrors grep: `0` matched, `1` nothing matched anywhere,
`2` an error or an unreachable machine.

> **If they ask** *"why does a dead machine get a word where a number should
> be?"* — because printing `0`, or leaving the row out, is indistinguishable
> from a machine that legitimately had no matches. Showing `UNREACHABLE`
> distinctly is what demonstrates the fault tolerance actually works.

---

## 3. The stops in detail

One section per component: what it does, why it is built that way, and what
breaks without it.

### 3.1 `config.hpp` / `config.cpp` — who is in the cluster

**`struct Machine`** — one machine: id, hostname, port.

**`LoadMachines(path, out, err)`** — parses `<id> <host> <port>` lines, skipping
blanks and `#` comments. Rejects, each against its line number: the wrong number
of fields, a non-numeric id or port, `id <= 0`, an id an earlier line already
claimed, a port outside 1–65535, and a file that parses cleanly but defines no
machines.

Two details worth knowing:

- **The port is range-checked before it is narrowed** to `uint16_t`. Otherwise
  `70000` silently becomes `4464` and the daemon binds a port nobody calls.
- **Hostnames are not resolved here.** A machine in the list is well-formed, not
  known to be up. DNS belongs at connect time, where `kUnreachable` already
  covers it; resolving during the parse would turn a config typo into a timeout
  and make startup depend on the network.

**`LogFileName(id)`** — returns `"machine.3.log"`. Trivial, and it is a shared
function rather than a format string repeated in three places because three
binaries have to agree on it: the daemon greps that file, the generator writes
it, the tests assert on it. Separate copies drift, and the symptom — a daemon
serving a file the generator never created — reads as a dead machine rather than
a typo.

### 3.2 `net.hpp` / `net.cpp` — moving bytes between machines

Think of a phone call.

| Function | The phone-call version | Why it exists |
|---|---|---|
| `Listen(port)` | get a phone number | claims the port so messages sent there arrive at this program |
| `Accept(fd)` | pick up when it rings | `Listen` only queues callers; `Accept` gives you a two-way channel with one of them |
| `Connect(host, port, timeout)` | dial a number | the querier's side, with the real timeout from Stop 4 |
| `IgnoreSigpipe()` | don't die when they hang up | see below |

**`IgnoreSigpipe()`** deserves its own paragraph. On Unix, writing to a
connection the other side already closed **kills your process** by default — not
an error code you can check. At the demo a grader kills a VM; without this, the
daemon writing to it dies too, and one failure becomes several. Both binaries
call it before they open a socket, and so does the test suite, which plays both
roles.

**`class Conn`** owns one socket and closes it in its destructor, so no error
path can leak one. It is movable but not copyable — two objects closing the same
descriptor would be a bug.

- **`WriteAll(data)`** — *say all of it.* A single `write()` may accept only part
  of what it was given; on a 24 MB result it almost always does.
- **`ReadLine(&line)`** — *hear one complete sentence.* Used for headers.
- **`ReadExactly(n, &out)`** — *hear a precise amount.* Used for payloads, where
  a header just announced the size. Accepting 12 bytes of a 19-byte chunk as
  complete is silent corruption — and a clean EOF partway through is exactly how
  a killed VM is detected.
- **`SetReadTimeout(ms)`** — *hang up if they go quiet.* A machine that crashes
  closes its socket and the read ends. A machine that **freezes** does neither.

> **Why `Conn` is a class and not three free functions:** `ReadLine` may read
> past the newline. Those extra bytes belong to the **next** message and have to
> survive until the next call. That buffer is the reason for the class, and
> `Conn_ReadLineHandlesSplitAndCoalescedReads` is the test that fails the moment
> someone throws them away.

`ReadLine` also refuses to buffer more than 64 KB without finding a newline: a
peer that sends more than that without one is not speaking this protocol, and
the alternative is buffering until the daemon dies.

### 3.3 `protocol.hpp` / `protocol.cpp` — turning bytes into messages

`net.cpp` moves bytes and has no idea what they mean. This file gives them
meaning. The format is **plain text on purpose** — you can point `nc` at a
daemon and read the whole exchange with your eyes.

```
client -> server      ARGS 2                 server -> client   D 19
                      2                                         machine.1.log:1423
                      -c                                        E 0 1
                      5
                      ERROR
```

That request is exactly what you can type into `nc 127.0.0.1 4425` by hand — a
useful demo, and a useful debugging tool when a daemon is misbehaving.

| Function | Direction | Does |
|---|---|---|
| `SendRequest` / `RecvRequest` | client → server | the grep arguments, length-prefixed |
| `SendData` / `RecvFrame` | server → client | `D <len>` + a chunk of grep's stdout |
| `SendEnd` / `RecvFrame` | server → client | `E <exit> <lines>`, the trailer from Stop 9 |

**Costs and buys.** Text costs a few bytes against a binary encoding and buys no
byte-order code, no bit shifting, and a protocol you can read over someone's
shoulder.

**Two framing details people ask about:**

- **Arguments end with a newline; data chunks do not.** The length delimits in
  both cases. For an argument, reading the terminator back as an *empty* line
  proves the declared length was right — a wrong one leaves the reader
  mid-argument and fails on the spot instead of shifting every field after it.
  Data chunks skip it because they are bulk, machine-read, and already carry
  grep's own newlines.
- **Every length is a number the peer chose**, so each is checked against a cap
  *before* anything is allocated: at most 256 arguments, 1 MB each, 4 MB for the
  request as a whole, and 8 MB for a single data chunk. Capping arguments
  individually is not enough — 256 × 1 MB would still let a stranger ask a
  daemon for a quarter of a gigabyte per connection.

**One deliberate coupling, documented on both sides:** every malformed-wire
message begins with `kWireErrorPrefix` (`"protocol: "`), which is how the client
tells "this peer is not a log-server" (`kProtocolError`) from "this peer died"
(`kPartial`) without a second error channel.

### 3.4 `grep_runner.hpp` / `grep_runner.cpp` — actually searching

**`RunGrep(user_args, log_path, sink, result, err)`** runs the **real** grep on
the local log and hands each chunk of its output to `sink`, which is the
callback that writes it to the socket.

Three decisions to be able to defend:

1. **`fork` + `execvp` with an argv array** — never `system()` or `sh -c`. See
   Stop 8.
2. **The filename is appended here**, from the server's own config, and the
   child `chdir`s into the log directory before exec'ing. That is why `-H`
   prints `machine.3.log:` and not `/home/you/mp1/machine.3.log:` — the spec
   asks for the filename, and grep echoes back whatever path it was given. The
   daemon's own working directory never changes; only the child's.
3. **`-H` leads the argument list** so every line carries its filename, but it
   comes *first* so a later flag wins: a user who passes `-h` or `-c` still gets
   what they asked for.

**Lines are counted while streaming.** Running grep a second time with `-c` to
get the number would re-read the whole 60 MB and roughly double the latency.

**The return value means something specific.** `RunGrep` returns `false` only
when grep could not be **run**. grep running and rejecting something — a bad
regex, a missing file — is a *successful* call whose `exit_code` happens to be
2, because "this machine is broken" and "your pattern is broken" are different
answers to give the querier.

### 3.5 `client.hpp` / `client.cpp` — asking everybody at once

**`enum MachineStatus`** — how one machine's answer turned out:

| | |
|---|---|
| `kOk` | answered, found matches |
| `kNoMatch` | answered, found nothing — **still a success** |
| `kGrepError` | answered; grep disliked the pattern |
| `kUnreachable` | could not connect — the machine is down |
| `kPartial` | started answering, then died mid-sentence |
| `kProtocolError` | said something we could not parse |

Six statuses rather than "ok / not ok" because the spec requires a failed
machine to be *reported*, and because these are the distinctions a person
debugging the cluster at 2 a.m. actually needs.

They are assigned by one rule, worth memorising for the demo:

> **Failure before the connection is up → `kUnreachable`. After it is up →
> `kPartial`. A peer that answered in something that is not this protocol →
> `kProtocolError`.**

**`QueryOne(machine, args, opts)`** — one conversation: connect, send, read
frames until END, fill in a result. **It never fails upward.** A dead machine is
a normal return value with a status, not an exception that aborts the query.
That is the entire fault-tolerance requirement, expressed as a return type — and
it is a `try`/`catch`, not just an intention, because a large result set can
still throw `bad_alloc`.

**`RunQuery(machines, args, opts)`** — the fan-out from Stop 3. Each task
returns a finished `MachineResult` **by value**, so there is no shared mutable
state and therefore no lock anywhere in the file. Waiting on a future *is* the
join.

**`PrintSummary`** — the table from Stop 10.

**`QueryExitCode`** — `0` / `1` / `2`, mirroring grep. It lives in the library
rather than in `main` so the test suite can assert on the same rule the binary
actually uses.

**The cost of this design, stated plainly:** each task buffers its machine's
matching lines in memory before returning them, so a frequent pattern over ten
60 MB logs can hold a few hundred MB in the querier. That is a deliberate trade
of memory for readability. The fix, when it is wanted, is to stream chunks to
stdout as they arrive under a mutex — at which point output from different
machines interleaves and the filename prefix on every line is what keeps it
sensible.

### 3.6 The three `main()` files

**`server_main.cpp` → `log-server`**

```
parse --id (required), --port, --log-dir
IgnoreSigpipe()
Listen(port)
forever:
    Accept a connection
    on a detached thread, inside a try/catch:
        RecvRequest
        RunGrep on my own log, SendData for each chunk
        SendEnd with grep's exit code and the counted lines
```

`--id` has no default on purpose: a daemon serving the wrong machine's log would
answer every query confidently and wrongly. A client that connects and then says
nothing is dropped after 30 seconds rather than pinning a thread forever.

**`client_main.cpp` → `log-query`**

```
IgnoreSigpipe()
parse my flags, keep the rest for grep
LoadMachines
RunQuery
PrintSummary
return QueryExitCode(summary)
```

**`gen_main.cpp` → `log-gen`**

```
--id, --seed, and either --lines or --bytes
GenerateLog
print the expected counts as EXPECT lines, so scripts and humans can use them
```

### 3.7 `log_gen.hpp` / `log_gen.cpp` — the oracle

The tests have to *automatically verify* that results are correct, which is only
possible if something knows the right answer in advance. This is that something,
and it is the reason the test suite can assert instead of eyeball.

**`GenerateLog(spec, path, expected, err)`** writes one machine's log and records
its ground truth. What makes the counts trustworthy:

- **Deterministic.** A `std::mt19937_64` seeded from the seed *and* the machine
  id — so one `--seed 42` gives every machine a different log, and gives each
  machine the same log every time. Never `rand()`, never the clock.
- **Realistic lines**: timestamp, level, component, message, so the demo looks
  like it is searching a service's log, because it is.
- **Planted tokens go at computed positions**, evenly spaced with a per-token
  phase offset, so the expected count is an exact number rather than a
  distribution — and so two patterns of the same frequency do not land on
  identical lines.
- **The filler vocabulary is lower-case words; every token is upper case with an
  underscore**, so a token cannot appear in filler text by accident.
- **And it does not trust any of that.** Every finished line is scanned for every
  token, and if the counts differ from what was planted, generation *fails* with
  a message. A filler/token collision therefore shows up here, loudly, instead of
  quietly making every expectation in the distributed suite too low.

**The three frequency classes** are defined here, in one place, so the tests, the
generator and `scripts/measure.sh` cannot disagree about what the words mean:

| Class | Frequency | Per 60 MB log (≈598,000 lines) | 4 machines |
|---|---|---|---|
| rare | `1e-5` | 6 lines | ~24 |
| somewhat frequent | `1e-3` | 598 lines | ~2,400 |
| frequent | `1e-1` | 59,795 lines | ~239,000 |

**`DefaultPatterns()`** plants one token per frequency class on every machine,
plus one that lands on machine 3 alone and one on the odd-numbered machines — so
a freshly deployed cluster can demonstrate every axis the spec asks about (rare
/ somewhat / frequent, and one / some / all logs) with nothing to regenerate and
nothing to remember at the demo.

### 3.8 `tests/` — the suite, and the throwaway cluster

**`Cluster::Start(n, base_port, err)`** launches `n` real `log-server` processes
on `127.0.0.1`, each with its own port and its own generated log. As far as the
code under test is concerned that is a genuine distributed system — real
sockets, real fan-out, real per-peer failures — and it runs in seconds on a
laptop instead of needing ten VMs.

- **Readiness is polled, never slept for.** A fixed sleep is either too short
  (flaky on a loaded VM) or too long (a slow suite). It connects in a loop, and
  fails early with somewhere to look if a daemon died instead of starting.
- **`Kill` uses `SIGKILL`, not `SIGTERM`.** A polite shutdown closes sockets
  properly and exercises a different code path than a VM that simply stops
  existing.
- **`StopAll` runs from the destructor**, so it happens even when a test fails —
  otherwise the next run collides with orphaned daemons still holding the ports.

**43 test cases, 408 assertions**, in two files:

| File | Count | Covers |
|---|---|---|
| `test_unit_local.cpp` | 22 | `Conn` over `socketpair()`, protocol round trips, config parsing, `RunGrep` edge cases, the generator's determinism and its oracle |
| `test_distributed.cpp` | 21 | the frequency axis, the distribution axis, output correctness, fault tolerance, concurrency, scale |

The framework is deliberately dependency-free (the spec allows "tests in a raw
manner"), with one addition worth knowing: **every case runs under a watchdog**.
A distributed suite has more ways to hang than to fail, and a hang tells you
nothing and blocks the run, so a case that overruns aborts the suite by name.

---

## 4. The questions you will actually be asked

**"Why is the fan-out concurrent?"**
Ten machines sequentially costs the sum of ten times; ten at once costs the
slowest. It is the whole performance argument, and it is also what makes a dead
machine cost *one* timeout rather than one per dead machine.

**"How do you know a machine failed, rather than just having no matches?"**
The END line. See Stop 9 — it is the best single answer in the project.

**"What happens if a machine dies in the middle of sending?"**
The END line's `line_count` is compared against the lines that actually arrived.
A mismatch, or no END at all, is `kPartial` — reported, and deliberately *not*
counted toward the total, because a number that might be wrong is worse than a
visible gap.

**"Why not just copy the logs to one machine and grep there?"**
§0's table. 600 MB on the wire to find ten lines, and one CPU doing what ten
could do at once.

**"Isn't this MapReduce?"**
No key space, no partitioning, no shuffle, no reduce phase, no framework
re-executing tasks. Scatter/gather fan-out RPC.

**"Why a text protocol?"**
A few bytes against a binary encoding, in exchange for no byte-order code, no
bit shifting, and the ability to drive the whole server from `nc` by hand. §3.3
has the exact bytes.

**"Why not write your own grep?"**
The spec forbids it, and that is a gift: shelling out to the real executable
gives every grep option, including arbitrary `-E` regexes, correctly and for
free.

**"Can two people query at the same time?"**
Yes — thread per connection, and `Distributed_ConcurrentQueriesDoNotInterfere`
runs four different queries simultaneously and checks each gets its own answer.

**"What if I point `log-query` at the wrong port?"**
`RecvFrame` rejects the first header and reports a protocol error naming what it
saw, rather than printing another service's bytes as though they were grep
output. `Protocol_RejectsGarbageHeader` covers it.

**"How do you know the tests actually test anything?"**
They were mutation-tested. Making the fan-out sequential, dropping grep's `-H`,
and removing the END-count check each fail exactly the test written for it.

---

## 5. Every trade-off, stated as a trade-off

| Decision | What it costs | What it buys |
|---|---|---|
| Grep locally, ship only matches | nothing, at this scale | 600 MB of network traffic, and 10× the search parallelism |
| One `std::async` per machine | a thread per machine per query | latency = slowest machine, not the sum; no locks, since each task returns by value |
| Buffer each machine's output before printing | a few hundred MB on a frequent query over ten 60 MB logs | readable code and non-interleaved output while learning |
| Text protocol | a few bytes per frame | no byte-order code; debuggable with `nc` |
| Real grep via `fork`/`execvp` | one process per query | every grep option, correct, free — and no shell for a stranger to reach |
| One pipe (stdout only) | grep's error text stays on the daemon | no `poll()` loop over two pipes, which deadlocks if you get it wrong |
| Static machine list | edit a file to change the cluster | no discovery protocol; the spec explicitly allows hard state |
| Thread per connection | a thread per concurrent query | simultaneous queriers do not queue behind each other |

---

## 6. Deliberately not built

Staged, not forgotten — say it that way, and say why:

- **A `MP1 <version>` greeting line.** Would make pointing `log-query` at the
  wrong port fail at hello rather than at the first frame. `RecvFrame` already
  rejects such a peer cleanly, so this buys a better message, not safety.
- **A second pipe for grep's stderr**, so `kGrepError` could report *why*. It
  needs a `poll()` loop over both pipes — draining one while grep fills the
  other deadlocks. Today the text lands in the daemon's own output and the
  querier still learns the exit code.
- **Rejoin after failure.** The spec assumes fail-stop and lists rejoin as an
  optional challenge.

Already done, and worth mentioning in the same breath so the list does not sound
like a list of holes: length caps on everything a peer chooses, `SIGPIPE`
ignored in all three binaries, a daemon that survives malformed requests, and
per-machine timeouts that keep one dead VM from touching the other nine.

---

## 7. Jump table

Line numbers are a snapshot; the function names are stable.

| Want to see… | Open |
|---|---|
| flag parsing, exit code | `src/client_main.cpp:59` |
| **the fan-out** | `src/client.cpp:145` `RunQuery` |
| one machine's conversation | `src/client.cpp:43` `Exchange` |
| status rules, `kPartial` detection | `src/client.cpp:88` |
| the summary table | `src/client.cpp:193` `PrintSummary` |
| the accept loop | `src/server_main.cpp:128` |
| one connection, start to finish | `src/server_main.cpp:72` `ServeOneRequest` |
| **`fork` + `execvp` + the drain loop** | `src/grep_runner.cpp:87` `RunGrep` |
| the wire format | `src/protocol.cpp` — `SendRequest:82`, `RecvFrame:95`, `RecvRequest:133` |
| length caps | `src/protocol.cpp:13` |
| **the connect timeout** | `src/net.cpp:86` `ConnectOne` |
| the read buffer that makes `Conn` a class | `src/net.cpp:167` `ReadLine` |
| the machine list parser | `src/config.cpp:68` `LoadMachines` |
| the oracle | `src/log_gen.cpp:186` `GenerateLog` |
| the throwaway cluster | `tests/harness.cpp` |
| what is tested | `tests/test_unit_local.cpp`, `tests/test_distributed.cpp` |

**Companion documents:** `README.md` (build and run), `MP1_PLAN.md` (what is
done and what is deliberately deferred), `MP1_BUILD_GUIDE.md` (the order it was
built in), `MP1_REQUIREMENTS.md` (what is graded). The design rationale for each
contract also lives in the header comments — `include/mp1/*.hpp` is the short
version of this document.
