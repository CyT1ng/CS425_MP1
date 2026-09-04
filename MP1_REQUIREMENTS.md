# CS425 MP1 — Distributed Log Querier (Fall 2026)

Summary of `MP1.CS425.FA26.pdf`.

## Key Dates
| Item | Date |
|---|---|
| Released | Aug 25, 2026 |
| Group form (exactly 2 students) | Mon, Aug 31, 2026 — hard deadline, required to get VMs |
| Code + report due | Sun, Sep 13, 2026, 11:59 PM (hard, no extensions) |
| Demo | Mon, Sep 14, 2026 |

Groups are submitted via the form on the course website's "Assignments" page — do **not** email group info. No form submission → no VMs → can't do the MPs. MPs apply to non-Coursera students enrolled for 4 credits.

## ⚠️ Explicit Restrictions
- **No MapReduce or MapReduce-like approaches.**
- Do not reuse remote-querying capabilities from external libraries (you may use libraries as components).
- Academic integrity: no looking at others' solutions (this year or past years). Moss is run. First offense = zero on MP, second = F in course. Discuss only spec/lecture concepts, never solutions/ideas/code; posting code on the forum = zero.

## Part I — Distributed grep
Build a program that queries distributed log files across machines from any one of them.

- N > 5 machines (N = 10 at the demo), each with a log file named `machine.i.log` (i = VM number).
- From a terminal on **any** of the N machines, run a grep that executes across all log files on all machines and prints output locally.
- Output must include **per-file line counts** with the **filename** the match came from. If invoking the grep executable directly, printing the number of lines grep returns is acceptable.
- Do **not** reimplement grep — call a library/system grep, and support all original grep options, especially arbitrary regexes via `-E`.
- **Performance is graded.** Design before coding. Consider: for infrequent patterns, does shipping all logs to the querier make sense? For frequent patterns? Should grep always run locally in parallel at each VM instead?
- Log-file generation is up to you (language print/cout, or e.g. Apache Commons Logging) — anything used must be installable on the CS VM Cluster at your permission level.
- A list of demo log files will be given closer to the demo. Must work at any log size; demo uses ~300,000-line files.

## Part II — Distributed Unit Tests
- Write **distributed** unit tests, not just local ones.
- Minimum required test: generate log files on every machine containing some known lines plus random lines, then run multiple greps and **automatically verify** results match expectations.
- Cover query patterns that are **rare, somewhat frequent, and frequent**, and patterns occurring in **one / some / all** logs.
- Tests need not be fast or short — they need to be as comprehensive as possible (explore most code paths, no manual intervention).
- A framework (googletest, junit, etc.) is fine; plain function calls are also acceptable.

## Fault Tolerance Requirements
- Must fetch answers from all machines that have **not** failed.
- Hard state (machine names / IPs) may be initialized statically.
- The querying machine never fails during a query; other log-holding machines may fail.
- **Any** machine must be able to act as the querier.
- Assume **fail-stop** (failed nodes never return). Node rejoin is an optional challenge, not required.

## Implementation Constraints
- Use **sockets** (or RPCs if using Go) — these are used in future MPs.
- Language: your choice; C++/Java/C/Go/Rust recommended ("Best MPs" are released only in these).
- Run on the **CS VM Cluster** (~10 VMs per group). Develop locally with multiple processes while VMs are provisioned.
- VMs have **no persistent storage** → use **git**. Course GitHub repo setup instructions posted on Piazza (by Aug 28); use a PAT or SSH key to clone on VMs.
- This is a "bootstrap" MP — you'll use the log querier to debug later MPs.

## Report (< 1 page, 12 pt, typed)
Must contain:
1. Brief design description (algorithm used).
2. Very brief description of unit tests (no need to detail each).
3. **Average query latency** — defined as time for the last machine to respond with the last byte — with **4 machines each storing 60 MB log files**.
4. Plot latency for (i) frequent, (ii) infrequent, (iii) rare queries.
   - ≥ **5 trials per data point**.
   - Plot **average and standard deviation**; SD must be **error bars on the same plot**, never a separate plot. (Common source of lost points.)
5. Prefer plots over tables/numbers in text, and **discuss** the plots — trends, whether they match expectations, and why.
- **1 point lost per line over the page limit.**

## Submission
1. Report to **Gradescope** before the deadline; **tag your pages** (penalty otherwise). One member submits and tags the partner.
2. Demo — signup sheet and instructions on Piazza. All group members attend in person (MCS Chicago students may Zoom).
3. Code via the **MP1 code submission form**. Repo must include a **README** covering: how to compile, how to run the unit tests, how to run MP1.

## Grading
| Component | Weight |
|---|---|
| Demo | 40% |
| Report (design + performance) | 40% |
| Code readability and comments | 20% |

## Group Contributions
Stick with the same group across MPs where possible (keeps VM mapping stable). All members are expected to contribute equivalently; if not, talk to them first and give a second chance, then approach an instructor.
