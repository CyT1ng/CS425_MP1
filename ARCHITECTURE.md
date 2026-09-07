# MP1 — Architecture

What every piece does, in plain language, and how they fit together.

Read this before writing code. `MP1_BUILD_GUIDE.md` says what order to build in;
this says what you are building.

---

## 1. What the project does, in one paragraph

Ten machines each hold their own log file. You sit at any one of them, type a
`grep` command, and get back the matching lines from **all ten**, labelled with
which machine each line came from — and if some machines are down, you still get
the answers from the ones that are up, with the dead ones clearly marked.

The trick is that the search happens **on each machine**, not on yours. Only the
matching lines travel over the network. Sending all ten 60 MB logs to one
machine and searching there would move 600 MB to find, sometimes, three lines.

---

## 2. The three programs

The `make` build produces four binaries. Three of them are the system:

| Binary | What it is | Runs |
|---|---|---|
| **`log-server`** | the **server**. Holds one log file, answers questions about it. | always, on all 10 machines |
| **`log-query`** | the **client**. Asks all ten servers the same question. | when you type a query |
| **`log-gen`** | makes fake log files with known contents, for testing | once, at setup |
| `run-tests` | the test suite | when you run `make test` |

Important: **every machine runs `log-server`, and every machine can run `log-query`.**
"Client" and "server" are roles, not machines. Machine 3 running `log-query` talks
to its own `log-server` over a socket, exactly like it talks to the other nine.

---

## 3. How one query works, start to finish

You type:

    ./bin/log-query -- -c ERROR

Then, in order:

**1. `log-query` reads the command line.** It separates its own flags (`--config`,
`--timeout-ms`) from the grep arguments (`-c ERROR`) that get passed through
untouched.

**2. `log-query` reads `config/machines.txt`** — the list of the ten machines, their
hostnames and ports. This is `LoadMachines`.

**3. `log-query` starts ten tasks at once**, one per machine. This is the single
most important design decision in the project. Doing them one at a time would
make the query take the *sum* of ten machines' time instead of the time of the
*slowest one*.

**4. Each task connects to its machine** and sends the grep arguments — and
**only** the grep arguments. It never sends a filename.

**5. Each `log-server` receives the request** and adds its own filename. Machine 3
searches `machine.3.log`, machine 7 searches `machine.7.log`. Nobody can be
tricked into searching the wrong file.

**6. Each `log-server` runs the real `grep`** as a separate program, on its own log,
and streams the matching lines back down the socket as they appear. It counts
lines as it goes.

**7. Each `log-server` finishes with an "END" message** carrying two numbers: grep's
exit code, and how many lines it sent. This is what lets the client tell "found
nothing" apart from "this machine died halfway".

**8. `log-query` waits for all ten tasks**, then prints the results and a summary
table with per-machine counts, and marks any machine that failed.

If a machine is down, step 4 fails for that one task after a short timeout. The
other nine are completely unaffected — they were never waiting on it.

---

## 4. The layers

Each layer only talks to the one below it. That is what makes each piece
testable on its own.

    log-query (client_main)      log-server (server_main)
          |                            |
      client.hpp                  grep_runner.hpp
      the fan-out                 runs the real grep
          |                            |
          +----------+-----------------+
                     |
              protocol.hpp
        turns bytes into messages
                     |
                net.hpp
             moves raw bytes
                     |
              the operating system

    config.hpp   — who the machines are (used by log-query)
    log_gen.hpp  — makes test logs with known contents (used by log-gen + tests)

---

## 5. Every function, by file

### `config.hpp` — who is in the cluster

**`struct Machine`** — one machine: its id number, hostname, and port.

**`LoadMachines(path, out, err)`**
Reads `config/machines.txt` and turns it into a list of `Machine`s.
*Why:* `log-query` has to know who to contact. Rather than discovering machines on
the network (complicated, and not what this MP is about), the list is just a
file you write by hand.
*All or nothing:* one bad line means zero machines and an error, never nine
machines and a shrug — silently querying a subset would be the worst possible
failure.

**`LogFileName(id)`**
Returns `"machine.3.log"` for id 3.
*Why a whole function for that:* three programs must agree on this name. `log-server`
reads that file, `log-gen` writes it, the tests check it. If the rule lived in
three places it would eventually differ in one, and the symptom would look like
a dead machine rather than a typo.

---

### `net.hpp` — moving bytes between machines

Think of a phone call.

**`Listen(port)`** — *get a phone number.* Claims port 4425 so messages sent
there arrive at your program. Without it nobody can reach `log-server` at all.

**`Accept(listen_fd)`** — *pick up when it rings.* `Listen` only gives you a
queue of waiting callers; `Accept` takes one out and gives you an actual two-way
channel with that one client. You end up with two sockets: the listening one
keeps listening, the accepted one is this conversation.

**`Connect(host, port, timeout)`** — *dial a number.* The client side.
*Why the timeout:* a machine can be dead in two ways. If the VM is up but `log-server`
isn't running, you get "refused" instantly. If the VM is **gone**, nothing
answers at all — and the operating system will wait *minutes* before giving up.
The timeout is what makes a dead machine cost two seconds instead of stalling
the whole query.

**`IgnoreSigpipe()`** — *don't die when they hang up.* On Unix, writing to a
connection the other side already closed **kills your program** by default. Not
an error you can check — the process is terminated. This turns that off.
*Why it matters:* at the demo a grader kills a VM. Without this, your server
tries to write to it and dies too. One failure becomes several.

**`class Conn`** — one live connection. It owns the socket and closes it
automatically when it goes out of scope, so no error path can leak one.

- **`WriteAll(data)`** — *say all of it.* A raw `write` may accept only **part**
  of what you gave it, like handing someone papers and they take half. This
  keeps offering the rest until it is all gone.

- **`ReadLine(&line)`** — *hear one complete sentence.* A raw `read` gives you
  whatever has arrived so far: maybe half a line, maybe three lines glued
  together. This gives you exactly one complete line. Used for **headers**.

- **`ReadExactly(n, &out)`** — *hear a precise amount.* Used for **payloads**,
  where a header just told you the size. Getting 12 bytes of a 19-byte chunk and
  treating it as complete is silent corruption.

- **`SetReadTimeout(ms)`** — *hang up if they go quiet.* A machine that crashes
  closes the connection and your read ends. A machine that **freezes** does
  neither, and a read on it waits forever.

*Why `Conn` is a class and not three loose functions:* `ReadLine` may read past
the newline. Those extra bytes belong to the **next** message and have to be
kept somewhere between calls. That memory is the reason for the class.

---

### `protocol.hpp` — turning bytes into messages

`net.hpp` moves bytes but has no idea what they mean. This file gives them
meaning.

The format is plain text, on purpose — you can point `nc` at a daemon and read
the whole conversation with your eyes.

    client -> server    ARGS 2
                        2
                        -c
                        5
                        ERROR

    server -> client    D 19
                        machine.1.log:1423
                        E 0 1

**`struct Request`** — the grep arguments. Note there is **no filename field**.
That is the security design: a client physically cannot ask a peer for
`/etc/passwd`, because there is nowhere to put the path.

**`struct Frame`** — one message from the server: either a chunk of output, or
the END marker with grep's exit code and line count.

**`SendRequest` / `RecvRequest`** — client sends the arguments, server reads
them. Each argument is sent with its length first, so an argument containing a
newline or a space survives intact.

**`SendData` / `RecvFrame`** — server sends a chunk of grep's output; client
reads it.

**`SendEnd`** — the important one. Without it, these three are byte-for-byte
identical on the wire, because all three are "the connection ended and there was
nothing in it":

- grep found nothing — *a correct answer*
- grep rejected the pattern — *an error*
- the machine was killed — *a dead peer*

`exit_code` separates the first two. `line_count`, checked against how many lines
actually arrived, catches a machine that died halfway through sending. And no
END at all means the machine vanished.

---

### `grep_runner.hpp` — actually searching

**`RunGrep(user_args, log_path, sink, result, err)`**
Runs the **real** `grep` program on the local log file and hands each chunk of
its output to `sink` (which sends it down the socket).

*Why run the real grep instead of writing our own:* the spec forbids
reimplementing it, and that is a gift — you get every one of grep's options,
including complicated `-E` regular expressions, working correctly, for free.

*Why not just call `system("grep ...")`:* that runs the command through a shell,
which would re-interpret the pattern. `-E '(foo|bar)+'` would break depending on
how the user happened to quote it. Worse, it would give a remote stranger a
shell on your VM.

**`struct GrepResult`** — the exit code and line count.
*Important:* grep's exit code **1 means "no lines matched"**, which is a
perfectly successful answer, not a failure. Only 2 is a real error. Confusing
those is the most common way to lose points here.

**`ChunkSink`** — a function the runner calls with each piece of output as it
appears. Streaming, rather than collecting the whole result first, is why a
24 MB answer doesn't have to fit in memory.

---

### `log_gen.hpp` — making test data

The tests have to *automatically verify* that the results are correct. That is
only possible if something knows the right answer in advance. This is that
something.

**`struct PatternSpec`** — one planted word: what it is, which machines get it,
and how often it appears.

**`struct LogSpec`** — the recipe for one machine's log: how many lines, which
patterns, and the random **seed**.

**`GenerateLog(spec, out_path, expected, err)`** — writes the log file and fills
in the expected counts.
*Must be deterministic:* the same seed must always produce a byte-identical
file. Use a seeded random generator, never the clock. Otherwise the "expected"
counts are fiction.
*The filler text must never accidentally contain a planted word*, or your
expected counts are too low and every test is subtly wrong.

**`struct ExpectedCounts` / `TotalFor(token)`** — the ground truth: for each
planted word, how many lines on each machine should match.

**`kRareFrequency` / `kSomewhatFrequency` / `kFrequentFrequency`** — the three
query classes the report has to measure, defined in one place so the tests and
the measurement script agree on what the words mean.

---

### `client.hpp` — asking everybody at once

**`enum MachineStatus`** — how one machine's answer turned out:

| | |
|---|---|
| `kOk` | answered, found matches |
| `kNoMatch` | answered, found nothing — **still a success** |
| `kGrepError` | answered, grep disliked the pattern |
| `kUnreachable` | could not connect — the machine is down |
| `kPartial` | started answering, then died mid-sentence |
| `kProtocolError` | said something we could not understand |

*Why so many:* the spec requires that a failed machine be **reported**, not
silently skipped. A machine dropped from the output looks exactly like a machine
that legitimately had zero matches.

**`StatusText(status)`** — turns those into words for the summary table.

**`struct MachineResult`** — one machine's complete answer: its output, its line
count, its status, and how long it took.

**`struct QueryOptions`** — timeouts and whether to print lines or just counts.

**`struct QuerySummary`** — everything, plus the totals and the wall-clock time
for the whole query.

**`QueryOne(machine, args, opts)`** — talk to **one** machine: connect, send the
request, read frames until END, fill in a result.
*The key rule:* this function must never fail upward. A dead machine is a normal
return value with `status = kUnreachable`. That is the entire fault-tolerance
requirement, written as a return type.

**`RunQuery(machines, args, opts)`** — the fan-out. Starts all ten `QueryOne`
calls **at the same time** and waits for all of them.
*Why concurrent:* this is the whole performance argument. Ten machines
sequentially = the sum of ten times. Ten at once = the time of the slowest one.
*Where the measured number comes from:* start the clock before launching, stop
it after the last one returns. That is exactly the spec's definition — "time for
the last machine to respond with the last byte."

**`PrintSummary(summary, opts)`** — prints the per-machine table. Per-file counts
with filenames are a hard spec requirement, and showing `UNREACHABLE` distinctly
from a count of zero is what demonstrates the fault tolerance actually works.

---

### `tests/harness.hpp` — a throwaway cluster for testing

**`Cluster::Start(n, base_port, err)`** — launches `n` real `log-server` processes on
your own machine, each on its own port with its own log file.
*Why this works:* as far as the code under test is concerned, that is a genuine
distributed system — real sockets, real fan-out, real failures. And it runs in
seconds on a laptop instead of needing ten VMs.
*Never use `sleep()` to wait for them to be ready.* Try connecting in a loop
until it works. Fixed sleeps are how a test suite becomes flaky.

**`Cluster::Kill(machine_id, err)`** — kills one daemon to simulate a machine
failing. Uses SIGKILL, not a polite shutdown — a polite shutdown closes the
socket properly and exercises a different code path than a VM that vanishes.

**`Cluster::StopAll()`** — cleans up, and must run even when a test fails, or
the next run collides with leftover daemons still holding the ports.

---

## 6. The three `main()` files

**`src/server_main.cpp` → `log-server`**

    read --id and --port
    IgnoreSigpipe()
    Listen(port)
    forever:
        Accept a client
        on a new thread:
            RecvRequest
            RunGrep on my own log, SendData for each chunk
            SendEnd with the exit code and line count

*Why a thread per connection:* several people may query at once, and the tests
certainly do. A single-threaded loop would make them queue up and look like a
hang.

**`src/client_main.cpp` → `log-query`**

    parse my flags, keep the rest for grep
    LoadMachines
    RunQuery
    PrintSummary
    return 0 (matched) / 1 (nothing matched) / 2 (something failed)

**`src/gen_main.cpp` → `log-gen`**

    read --id, --seed, --lines
    GenerateLog
    print the expected counts so scripts and tests can use them

---

## 7. Why `client.cpp` and `client_main.cpp` are separate files

`client_main.cpp` has `main()` — it deals with the command line.
`client.cpp` has `RunQuery` — it deals with the cluster.

The reason they are split: **the test program also needs to query the cluster**,
and it has its own `main()`. A program can only have one `main`, and you cannot
call half of somebody else's. So the querying logic lives in a file that both
programs can use.

Without the split, the tests would have to run `./bin/log-query` as a subprocess and
read numbers back out of its printed table with string parsing — and every
change to the table's spacing would break the tests.
