# MP1 — Plan and Checklist

Companion to `MP1_REQUIREMENTS.md`. Working document: tick boxes as you go.

> **Course rule, from the spec — not in the requirements summary:**

> The skeleton in this repo is structure, contracts, and test names. Every
> function body is a `TODO` on purpose. Write them yourself, and make sure both
> partners can explain any line of the result cold.

**Code + report due Sun Sep 13, 11:59 PM** (hard, no extensions). Demo Mon Sep 14.

For the order to build it in, with a check at each step, see `MP1_BUILD_GUIDE.md`.

---

## 0. Do this first

- [ ] Group form submitted (was due Aug 31 — if it slipped, email staff **today**;
      no form means no VMs means no MP)
- [ ] Course GitHub repo created, both partners have push access
- [ ] PAT or SSH key working, so you can clone on a VM
- [x] VMs assigned — group 91, `fa26-cs425-9101..9110`, in `config/machines.txt`
- [ ] `make && make test` runs on your laptop *and* on one VM

---

## 1. The design, in one paragraph

Every machine runs `log-server`, a daemon holding its own `machine.i.log`. Any
machine can run `log-query`, which opens one connection per machine **in
parallel**, sends the grep arguments, and each daemon runs the real system
`grep` on its own local log and streams matching lines back. Only matches cross
the network.

**Ship the query to the data, not the data to the query.** This is the decision
the spec asks you to justify, so be ready for it at the demo:

| | Fetch logs to querier | Grep locally (this design) |
|---|---|---|
| Bytes on the wire | 60 MB × N, always | only the matches |
| Where matching happens | one CPU, serially | N machines, concurrently |
| Rare pattern | 240 MB moved to find ~10 lines | a few KB |
| Frequent pattern | 240 MB moved | ~24 MB |

The two only converge if a pattern matches ~100% of lines. Local grep never
loses.

**Why this is not MapReduce** (the spec bans it): no key space, no partitioning,
no shuffle, no reduce phase, no framework re-executing tasks. It is a
scatter/gather fan-out RPC. Say exactly that if asked.

---

## 2. Implementation order

Each phase ends in something you can run. Don't start a phase before the one
above it is green.

### Phase A — plumbing (`net`, `protocol`, `config`)

Two staged simplifications, both deliberate and both written down where the
code is: the wire format is **text** (`include/mp1/protocol.hpp`), and the
hardening it defers is Phase D. Say "we staged it" at the demo, not "we forgot".

- [x] `LoadMachines`, `LogFileName`
- [ ] `Conn::ReadLine` / `ReadExactly` / `WriteAll`. The internal buffer is the
      whole trick: one `read()` can stop mid-line or return two lines at once,
      so leftover bytes have to survive to the next call
- [ ] `Listen`, `Accept`, `Connect`, `IgnoreSigpipe`. `Connect` needs
      non-blocking + `poll(POLLOUT)` + `SO_ERROR` — Linux ignores socket
      timeouts on `connect()`, so `SO_RCVTIMEO` will not save you here
- [ ] `SendRequest` / `RecvRequest`, `SendData` / `SendEnd` / `RecvFrame`
- [ ] `make test` green for the `Conn_*` and `Protocol_*` tests

### Phase B — one machine end to end
- [ ] `RunGrep`: `pipe` + `fork` + `execvp`, one pipe for stdout. Close the
      write end in the parent **first**, or the pipe never reports EOF because
      you are still holding it open yourself. grep's stderr is inherited for
      now; the second pipe arrives with the error frame in Phase D
- [ ] Count lines while streaming — never run grep twice
- [ ] `log-server` accept loop, thread per connection, survives a malformed
      request
- [ ] `log-query` against a single local daemon returns correct output
- [ ] `log-gen` deterministic, and its expected counts match a real local grep

### Phase C — the fan-out
- [ ] `QueryOne`: connect, send, drain frames, fill a `MachineResult`. It must
      never throw — a dead machine is a normal return value, not an error
- [ ] `RunQuery`: one `std::async(std::launch::async, ...)` per machine, collect
      every future. **`launch::async` is not optional** — without it the task
      may be deferred to `get()`, which silently makes the fan-out sequential
      and quietly ruins the latency numbers
- [ ] `wall_latency` = max, not sum
- [ ] `PrintSummary`: per-machine table with filename + line count
- [ ] `./scripts/start_cluster.sh 6`, then query it with
      `--config config/local.txt` from "any" machine

### Phase D — protocol hardening, then fault tolerance

Hardening first: `UNREACHABLE` and `PARTIAL` are only trustworthy once a peer
cannot crash you or lie to you about a length.

- [ ] A `MP1 <version>\n` greeting line. Catches pointing `log-query` at the
      wrong port — a stale daemon, another service — instead of parsing
      someone else's bytes as grep output. The port already moved 9425 → 4425
      once
- [ ] Length caps on the request and on each frame, checked *before* any
      allocation. A length is a number the **peer** chose
- [ ] A second pipe for grep's stderr and an error frame to carry the text, so
      `kGrepError` reports *why*. Two pipes means `poll()`ing both: drain only
      one while grep fills the other and it deadlocks
- [ ] Dead machine → `UNREACHABLE`, reported, not silently dropped
- [ ] Dead machine does **not** delay live ones (test against a blackhole IP,
      not localhost — localhost gives an instant refusal and proves nothing)
- [ ] Killed mid-stream → `PARTIAL`, caught by comparing the END line's count
      against the lines actually received
- [ ] `SIGPIPE` ignored, or a disconnecting client kills your daemon

### Phase E — the test suite
- [ ] `Cluster` harness: spawn N daemons, poll for readiness (**never** a fixed
      `sleep`), kill one, clean up even when a test fails
- [ ] Every named test in `tests/test_distributed.cpp` implemented
- [ ] Frequency axis: rare / somewhat frequent / frequent
- [ ] Distribution axis: one log / some logs / all logs / no log
- [ ] Whole suite passes 10 runs in a row without a flake

### Phase F — the cluster
- [ ] `deploy.sh` works against all 10 VMs from scratch
- [ ] N=10, ~300,000-line logs, query from several different machines
- [ ] Rehearse a reboot: VMs have **no persistent storage**, so clone, build and
      log generation all have to happen again

### Phase G — measurements and report
- [ ] 4 machines × 60 MB logs
- [ ] `measure.sh`, ≥5 trials per class (7 is safer), warm-up discarded
- [ ] `plot_latency.py` → one plot, mean **and** SD as error bars
- [ ] Report written, under one page, plots discussed
- [ ] README accurate — recheck the commands actually work as written

---

## 3. What the report has to contain

Under **one page**, 12 pt. One point lost per line over.

- [ ] Design description (the algorithm — section 1 above, compressed)
- [ ] Very brief description of the unit tests
- [ ] Average query latency, 4 machines × 60 MB, defined as *time for the last
      machine to respond with the last byte*
- [ ] Plot for frequent / infrequent / rare, ≥5 trials each
- [ ] **Mean and SD on the same plot, SD as error bars** — the single most
      common way points are lost here
- [ ] Discussion of the trends, not just the figure

**The discussion writes itself if you notice this:** grep reads all 60 MB
regardless of how many lines match, so the *search* cost is nearly identical
across all three query classes. What changes is the size of the result set, so
the frequent query is dominated by network transfer, and the rare and infrequent
queries are nearly indistinguishable. Two further points worth a sentence each:

- The querier's inbound link is shared, so for frequent queries latency scales
  with N even though the greps are perfectly parallel. That is the real
  bottleneck in this design, and it is worth naming.
- SD should be largest for the frequent query — a longer transfer has more
  opportunity to collide with other traffic. If your data does not show that,
  say so and explain why rather than glossing over it.

Define "rare"/"infrequent"/"frequent" by actual match count in the report. Those
words mean nothing to a grader on their own.

---

## 4. Demo checklist (Mon Sep 14)

- [ ] All 10 VMs up, daemons running, logs generated
- [ ] Query from at least three different machines
- [ ] Kill a VM mid-demo, re-run, show live machines still answer correctly
- [ ] Have `-E` regexes, `-c`, `-i`, `-v` ready to type
- [ ] Both partners can explain: the protocol framing, why the fan-out is
      threaded, how a failure is detected, why this isn't MapReduce
- [ ] Signed up on the Piazza sheet, both attending in person

---

## 5. Traps that cost points

- Sequential fan-out. Latency becomes the sum, and it shows up in the plot.
- Treating grep's exit code 1 (no match) as an error. It is a valid answer.
- Omitting a dead machine from the output instead of marking it failed — it
  looks identical to a machine with zero matches.
- Assuming one `read()` returns a whole message.
- Demoing before Phase D. Without the length caps a malformed frame can take a
  daemon down, and "do not let a MALFORMED request kill the daemon" is a stated
  requirement in `src/server_main.cpp`.
- Forgetting to close pipe write-ends in the parent, so you never see EOF.
- `system()` or `sh -c` instead of `execvp` — breaks `-E` patterns depending on
  quoting, and hands a remote caller a shell.
- Committing a 60 MB log file. It is permanent in git history.
- Untagged Gradescope pages, and not tagging your partner.
- Code readability is **20%** of the grade. Budget real time for comments.

---

## 6. Submission

- [ ] Report to Gradescope before the deadline, **pages tagged**, partner tagged
- [ ] Code via the MP1 code submission form
- [ ] Repo has a README covering compile / run tests / run MP1
