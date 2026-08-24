# Flush+Monitor Page-Cache Side-Channel Project
## Execution roadmap for Codex

> Project goal: reproduce the Flush+Monitor page-cache side channel from the NDSS 2026 paper
> **"Eviction Notice: Reviving and Advancing Page Cache Attacks"**, validate it on the local
> environment, adapt it to a controlled victim, evaluate it quantitatively, and demonstrate a
> simple mitigation.
>
> This is a controlled academic reproduction. Do not target third-party systems, real users,
> sensitive data, or software outside the local experimental environment.

---

# 0. Operating rules for Codex

This is a 3–4 day academic experiment. Optimize for **clarity, minimal code, and fast iteration**.

1. **Keep everything minimal.**
   Do not build abstractions, frameworks, helpers, wrappers, configuration systems, or generic tooling
   unless the experiment strictly needs them.

2. **Code should look hand-written, not generated.**
   Avoid LLM-style patterns:
   - no obvious one-line comments that merely restate the code;
   - no excessive section comments;
   - no verbose function names when a short clear name is enough;
   - no gratuitous docstrings;
   - no boilerplate error-message framework;
   - no over-factored helper functions;
   - no unnecessary classes or indirection;
   - no speculative extensibility.

3. **Comments only when they explain something non-obvious.**
   Especially comment:
   - kernel/API quirks;
   - page-cache assumptions;
   - alignment requirements;
   - why a syscall result means cached/not-cached.
   Do not comment trivial C/Python syntax.

4. **Do not over-defend the code.**
   Add only checks required to:
   - avoid invalid experiment results;
   - make failures understandable;
   - prevent obvious misuse of the experiment.
   Do not add enterprise-style validation or redundant guards.

5. **Keep output quiet.**
   Print only what is useful for the experiment or demo.
   No verbose progress logging unless debugging.

6. **Record only what we may actually need later.**
   Do not create a file for every command, run, or observation.
   Prefer:
   - one short `notes.md`;
   - one baseline CSV;
   - one mitigation CSV;
   - one sweep CSV;
   - final plots.
   Raw terminal output should only be saved when it provides useful evidence.

7. **Do not create files just because the roadmap suggests a directory.**
   Create the minimum set of files needed by the implementation.

8. **Do not reimplement the whole paper.**
   Scope is Flush+Monitor only.

9. **Reproduce before modifying.**
   Make the relevant upstream artifact work first.
   Do not change upstream artifact source for the baseline.

10. **Use upstream `monitor-preadv2` as the reference Monitor.**
    Keep modern-kernel adaptations separate. `cachestat` is validation-only
    where useful and must not become a blocker.

11. **No eviction, eviction sets, website fingerprinting, keystroke timing, covert channels,
    or unrelated attacks.**

12. **Docker is not a kernel fallback.**
    Use containers only for userspace/dependency convenience.
    Use a VM only if a kernel compatibility issue actually appears.

13. **Do not invent results.**
    All metrics must come from measured data.

14. **Stop at milestone boundaries.**
    At the end of a milestone, give a short report in chat.
    Update the roadmap status minimally.
    Do not write long reports unless explicitly requested.

15. **Prefer deleting code over generalizing it.**
    If two implementations solve the same problem, keep the smaller one that is easier to explain in a slide.

# 1. Final deliverables

- [ ] Upstream artifact cloned and preserved.
- [ ] Experimental environment documented.
- [ ] Flush primitive verified.
- [ ] Monitor using `preadv2(..., RWF_NOWAIT)` verified.
- [ ] Cross-user page-cache observation verified.
- [ ] Artifact's local `htop` detection PoC reproduced.
- [x] Temporal-resolution comparison of the reference and two modern Monitors.
- [x] Cross-user threat model verified for both modern Monitor variants.
- [x] Controlled victim implemented.
- [x] Flush+Monitor detects a controlled victim event.
- [x] Small custom attacker / attack harness implemented if useful.
- [x] Automated randomized trials with ground truth.
- [x] Confusion matrix.
- [x] Accuracy, precision, recall, F1, FPR/FNR.
- [ ] At least one parameter/robustness experiment.
- [ ] Simple mitigation implemented.
- [ ] Same benchmark repeated after mitigation.
- [ ] Before/after comparison.
- [ ] Clean demo scripts.
- [ ] README with reproduction instructions.
- [ ] Presentation-ready figures/tables.
- [ ] Threat-model material prepared for slides.

---

# 2. Intended project structure

```text
pagecache-project/
├── PROJECT_ROADMAP.md
├── README.md
├── artifact/
│   └── Eviction-Notice/
├── notes/
│   ├── environment.md
│   ├── artifact-reproduction.md
│   ├── threat-model.md
│   └── experiment-log.md
├── victim/
│   ├── victim.c
│   ├── victim_direct.c
│   └── Makefile
├── attacker/
│   ├── attacker.c
│   └── Makefile
├── experiments/
│   ├── run_trials.py
│   ├── analyze.py
│   ├── sweep_interval.py
│   └── stress.sh
├── data/
│   ├── raw/
│   └── processed/
├── plots/
└── demo/
    ├── baseline.sh
    └── mitigated.sh
```

Do not create empty complexity for its own sake.

---

# 3. Experimental story

```text
Paper / artifact
      ↓
Reproduce primitives
      ↓
Reproduce documented htop PoC
      ↓
Compare reference and modern Monitor cycles
      ↓
Build controlled victim
      ↓
Detect controlled sensitive event
      ↓
Automate repeated trials
      ↓
Measure detection quality
      ↓
Apply mitigation
      ↓
Repeat exact benchmark
      ↓
Compare security and performance
```

---

# 4. Threat model

## Victim
Normal local process running as Unix user A.

Victim supports:
- EVENT=0: no access to the monitored page;
- EVENT=1: controlled "sensitive operation" that accesses the target page.

Victim writes independent ground-truth logs.

## Attacker
Normal local process running as a different Unix user B where practical.

Attacker may:
- know target file and page/offset;
- open/read the shared target file if required;
- use unprivileged Linux syscalls;
- repeatedly Flush and Monitor.

The modern Monitor candidates retain this intended attacker model: a normal
different-UID process needs only read access to the same shared inode and
unprivileged syscalls. `RWF_DONTCACHE` adds a kernel/filesystem compatibility
requirement, not an attacker privilege. M3A verified this equivalence for both
variants; validation tools such as `mincore`/`cachestat` do not supply the
attack signal.

Attacker may not:
- use root-only introspection as the attack mechanism;
- ptrace the victim;
- read victim memory;
- inject code;
- read victim ground truth during inference;
- target third-party systems.

## Security property
Leakage of victim **activity**:
> "Did the victim execute operation X during this observation window?"

---

# 5. Core mechanism

```text
FLUSH target page
        ↓
observation window
        ↓
victim may access target page
        ↓
MONITOR target page
        ↓
cached => infer event
not cached => infer no event
```

Primary Monitor:
```text
preadv2(..., RWF_NOWAIT)
```

Take exact return/error interpretation from upstream source and document it.

Modern comparison variants:

```text
A. Flush -> observation -> preadv2(RWF_NOWAIT) -> wait for I/O -> Flush
B. Flush -> observation -> preadv2(RWF_NOWAIT | RWF_DONTCACHE)
   -> on miss, repeat until drop-behind restores absence
```

Both are state-restoring at the sample boundary, but transiently contaminating.

---

# 6. Milestone overview

| Milestone | Goal | Must-have output |
|---|---|---|
| M0 | Environment + repository bootstrap | `notes/environment.md` |
| M1 | Verify Flush + Monitor primitives | reproducible transcript |
| M2 | Verify cross-user observation | cross-user result |
| M3 | Reproduce documented `htop` PoC | baseline attack evidence |
| M3A | Compare Monitor temporal resolution | three-way timing table |
| M4 | Build controlled victim | victim source + ground truth |
| M5 | Detect our victim | controlled attack demo |
| M6 | Automate experiment | randomized synchronized trials |
| M7 | Compute baseline metrics | CSV + confusion matrix + metrics |
| M8 | Parameter/robustness sweep | at least one plot |
| M9 | Implement/evaluate mitigation | before/after data |
| M10 | Package demo + slide assets | scripts, README, figures |

---

# 7. M0 — Bootstrap and environment

## Actions
1. Create workspace and roadmap.
2. Clone upstream artifact into `artifact/Eviction-Notice`.
3. Record upstream commit hash.
4. Do not modify upstream source.
5. Collect:
```bash
uname -a
uname -r
cat /etc/os-release
ldd --version | head -1
getconf PAGESIZE
findmnt /
findmnt /tmp
stat -f -c '%T' .
gcc --version | head -1
make --version | head -1
```
6. Record CPU model/architecture.
7. Record whether `/tmp` is tmpfs.
8. Select a disk-backed target directory.
9. Read only upstream README sections for Requirements, Flush, Monitor, Detecting Programs.
10. Inspect only source needed for Flush and `monitor-preadv2`.
11. Summarize logic in `notes/artifact-reproduction.md`.

## Acceptance
Environment and compatibility documented; no upstream modifications.

---

# 8. M1 — Verify Flush and Monitor

Goal:
```text
read => cached
flush => not cached
```
verified with `monitor-preadv2`.

Actions:
1. Build required primitives according to upstream instructions.
2. Create a sufficiently large disk-backed test file.
3. Populate cache.
4. Run `monitor-preadv2`; confirm cached.
5. Flush.
6. Run `monitor-preadv2`; confirm uncached.
7. Repeat >=3 times.
8. Save raw output as `data/raw/m1-*`.

Failure checks:
- tmpfs?
- filesystem compatibility?
- return codes?
- compare with upstream README;
- use `strace` only as debugging aid.

---

# 9. M2 — Cross-user validation

Goal: user B observes cache state caused by user A.

Actions:
1. Use/create a second test Unix user if practical.
2. Configure minimal sufficient permissions.
3. Attacker flushes/monitors.
4. Victim reads target.
5. Attacker observes transition.
6. Repeat and record ownership/permissions.

Acceptance: observation works without ptrace/root as attack signal.

Fallback: validate cross-process first and document cross-user blocker separately.

---

# 10. M3 — Reproduce documented `htop` PoC

Use the upstream README's "Detecting Programs" procedure.

Concept:
```text
attacker: flush /usr/bin/htop
attacker: monitor /usr/bin/htop
victim:   execute htop
attacker: observe pages become resident
```

Actions:
1. Ensure no existing `htop` is running.
2. Resolve actual binary.
3. Run documented Flush.
4. Run documented Monitor loop.
5. Execute `htop` on victim side.
6. Capture exact terminal output.
7. Repeat cleanly >=2 times.
8. Record spurious detections.

Interpretation:
No process-table/victim-memory inspection. Inference is based on page-cache residency.

---

# 10A. M3A — Monitor temporal-resolution comparison

Compare three mechanisms:

1. reference Monitor from the paper on the compatible ext4 VM
   (`preadv2(..., RWF_NOWAIT)`, kernel 5.15);
2. modern `Flush -> Monitor -> wait for submitted I/O -> Flush`;
3. modern `RWF_NOWAIT | RWF_DONTCACHE` state-restoring candidate.

Use the same page size, target size/offset, cached and uncached starting states,
iteration count, CPU affinity and reporting format where possible. Record
environment differences explicitly. Do not present host-versus-VM latency as a
pure syscall comparison unless the guests are matched apart from the kernel.

Measure at least 10,000 hit and 10,000 miss samples per mechanism:

- time to classification;
- time from classification to restored state;
- full sample-cycle time;
- p50/p95/p99 and cleanup-failure rate;
- theoretical maximum sampling frequency.

For the two modern variants, also repeat the functional test cross-UID on the
same readable shared inode. The attacker must use only Flush/Monitor syscalls;
owner/root cache inspection is permitted only as independent validation.

Acceptance:

- one comparable table with raw data and exact environment labels;
- no immediate-Flush race presented as a valid cleanup;
- cross-UID pass/fail recorded separately for both modern variants;
- candidate limitations include transient contamination and filesystem support.

---

# 11. M4 — Controlled victim

Create a disk-backed `target.bin` with page-aligned target offset.

Victim states:
```text
EVENT=0: no target-page access
EVENT=1: explicit pread of exactly target page
```

Requirements:
- page size obtained programmatically where sensible;
- page-aligned offsets;
- monotonic timestamps;
- trial IDs;
- independent ground-truth CSV;
- interactive demo mode;
- automated experiment mode;
- simple Makefile.

Do not begin with mmap unless needed.

Acceptance:
EVENT=1 reliably changes expected page-cache state.

---

# 12. M5 — Controlled Flush+Monitor attack

First use upstream primitives directly against our victim.

Then, only if useful, implement minimal `attacker.c`.

Pseudo-code:
```text
for trial:
    flush(target_page)
    synchronize trial start
    observation window
    monitor(target_page)
    prediction = cached ? 1 : 0
    log prediction
```

Attacker log:
```text
trial_id,timestamp,target_page,observation_window,monitor_result,prediction
```

Attacker must not access victim ground truth.

Acceptance:
clean two-terminal EVENT / NO-EVENT demo.

---

# 13. M6 — Automated randomized experiment

Start:
```text
N=200
P(EVENT)=0.5
```
Then increase toward 500 if stable.

Use explicit trial IDs and separate files:
```csv
trial_id,timestamp,event
```
and
```csv
trial_id,timestamp,prediction,monitor_result
```

Synchronization may coordinate trial boundaries but must not transmit the event bit.

Prefer explicit local IPC/barrier over timestamp matching.

Acceptance:
every trial has exactly one ground-truth and prediction row.

---

# 14. M7 — Baseline metrics

Compute:
- TP/TN/FP/FN
- accuracy
- precision
- recall
- F1
- FPR
- FNR

Generate:
- `data/processed/baseline_metrics.csv`
- confusion matrix
- compact metrics table
- short true-event vs detection timeline

One analysis command must reproduce outputs from raw data.

---

# 15. M8 — Parameter / robustness experiment

Required: sweep observation/polling interval.

Initial candidates:
```text
100 us
500 us
1 ms
5 ms
10 ms
```
Adapt only based on measured host behaviour.

For each:
- fresh randomized experiment;
- distinct raw data;
- precision/recall/F1/FPR/FNR;
- runtime/configuration recorded.

Generate:
```text
F1 vs observation interval
```

Optional after completion:
idle vs controlled background load.

---

# 16. M9 — Mitigation

Preferred: controlled victim variant using `O_DIRECT`, if filesystem supports it.

Before coding:
- inspect filesystem direct-I/O requirements;
- determine alignment constraints;
- plan aligned buffer + aligned offset.

Then:
- implement `victim_direct.c`;
- verify access succeeds;
- verify monitored cache signal is reduced/removed;
- measure victim-operation latency;
- repeat the exact baseline benchmark.

Do not claim `O_DIRECT` is a universal Linux mitigation.

Framing:
> Application-level mitigation for our controlled victim that removes the shared page-cache signal associated with the sensitive operation.

Fallback if `O_DIRECT` is a time sink:
use a private file/inode for victim so attacker does not share the same page-cache identity.

Generate before/after security metrics + latency.

---

# 17. M10 — Demo packaging

Prefer:
```bash
./demo/baseline.sh
./demo/mitigated.sh
```

Requirements:
- clear VICTIM/ATTACKER labels;
- target/page/config printed;
- detected events obvious;
- loud prerequisite failures;
- no attacker access to ground truth.

README:
1. prerequisites
2. build
3. baseline demo
4. automated experiment
5. analysis
6. mitigated demo
7. limitations

Prepare a backup-demo recording checklist.

---

# 18. Presentation assets

Prepare data/diagrams for ~16–20 slides:

1. page-cache sharing
2. side-channel leakage
3. Flush
4. Monitor
5. Flush+Monitor timeline
6. threat model
7. setup table
8. `htop` reproduction evidence
9. controlled victim
10. experiment architecture
11. confusion matrix
12. baseline metrics
13. F1 vs interval
14. mitigation
15. before/after
16. limitations/conclusion

Suggested narrative:
Title → motivation → page cache → leakage → paper taxonomy → Flush → Monitor → threat model → reproduction → victim → demo → evaluation → results → sweep → mitigation → before/after → limitations → conclusion.

---

# 19. Scope fence

Mandatory:
artifact primitives → `htop` PoC → victim → controlled attack → threat model → metrics → mitigation → before/after → clean demo.

Valuable:
interval sweep → latency overhead → load robustness.

Cut first:
complex attacker architecture → extra mitigations → elaborate UI → orchestration.

Never enter scope unless explicitly requested:
Evict, eviction sets, Flush+Reload, Flush+Flush, website fingerprinting, inter-keystroke attacks, browser targeting, real sensitive applications, kernel exploitation.

---

# 20. Environment fallback

```text
Artifact fails
    |
    +-- primitive fails?
    |      +-- userspace/fs/build issue -> fix path/dependency / documented Debian/Ubuntu userspace
    |      +-- kernel behaviour issue -> VM with compatible environment
    |
    +-- primitive works, PoC fails
           -> simplify target and debug experiment
```

Docker changes userspace; Docker does not replace the host kernel.

---

# 21. Status log

## Current milestone
`M8`

## Completed
- [x] M0
- [x] M1
- [x] M2
- [x] M3
- [x] M3A
- [x] M4
- [x] M5
- [x] M6
- [x] M7
- [ ] M8
- [ ] M9
- [ ] M10

## Current blockers
None.

## Decisions
- Preserve the paper Monitor on kernel 5.15/ext4 as the reference condition.
- Evaluate two separate modern state-restoring cycles; do not describe either
  as non-contaminating.
- Use matched Ubuntu/ext4/QEMU guests and vary the kernel condition only.
- The two modern variants retain the same cross-UID privilege model in the
  tested ext4 environment; `RWF_DONTCACHE` remains filesystem-dependent.
- Use the same seeded event sequence and a fixed 50 ms barrier for all three
  randomized baseline conditions.

## Latest measured results
- M7 baseline: all three primitives measured TP=103, TN=97, FP=0, FN=0;
  accuracy/precision/recall/F1 = 1.0 and FPR/FNR = 0.0 in the controlled
  50 ms-window experiment. See `data/processed/baseline_*`.
- M6 randomized experiment: 200 trials per primitive, with one attacker row and
  one ground-truth row for every ID 1--200. All conditions used the same event
  sequence; the attacker could not read ground truth. Metrics remain for M7.
- M5 controlled detection: upstream Monitor on kernel 5.15, modern wait+Flush,
  and modern `RWF_DONTCACHE` each matched two EVENT and two NO-EVENT trials
  (4/4 per primitive). Attacker UID 997 could not read victim UID 996's mode
  `0600` ground truth. See `data/raw/m5-*`.
- M4 victim: three `EVENT=0` trials kept target page 0 absent and three
  `EVENT=1` trials changed it from absent to cached. Automated and interactive
  modes both produced independent monotonic ground truth.
- Matched-VM M3A, 10,000 samples per state and mechanism: miss p50 full cycle
  0.232 us for reference 5.15, 13.689 us for modern wait+Flush, and 13.169 us
  for modern `RWF_DONTCACHE`; all cleanup-failure rates were 0/10,000.
- Hit p50 full cycles were 0.777, 0.814, and 0.820 us respectively.
- Both modern variants passed three cross-UID hit and three miss trials using a
  victim-owned `0644` inode. See `notes/environment.md` and `data/raw/m3a-*`.
- An immediate Flush after `EAGAIN` is still invalid: it races asynchronous I/O.

---

# 22. Definition of Done

We can make evidence-backed claims that:
1. relevant artifact primitives were reproduced;
2. documented `htop` program detection was reproduced;
3. controlled victim was created;
4. a separate local process/user inferred a controlled victim event via page-cache state;
5. repeated randomized trials were evaluated;
6. precision/recall/F1/error rates were measured;
7. at least one operational parameter was studied;
8. a simple mitigation was implemented;
9. the same evaluation was repeated after mitigation;
10. baseline and mitigated behaviour can be demoed reproducibly.

---

# Codex milestone prompts

## PROMPT 0 — Master bootstrap

Minimalism rule for this milestone:
- write the smallest code that works;
- do not add abstractions, comments, checks, files, or logs unless directly required;
- avoid LLM-style comments and boilerplate;
- prefer editing one existing file over creating several new ones;
- keep milestone reports short.


You are working inside a local academic cybersecurity project.

Your job is to reproduce, evaluate, and mitigate the Flush+Monitor Linux page-cache side channel from the NDSS 2026 paper "Eviction Notice: Reviving and Advancing Page Cache Attacks".

First, create `PROJECT_ROADMAP.md` in the project root containing the roadmap I provide, preserving it as the source of truth.

Then:
1. Read the entire roadmap.
2. Do NOT implement the custom victim/attacker yet.
3. Execute only milestone M0.
4. Inspect upstream README and only source needed to understand Flush and Monitor with `preadv2`.
5. Record host/environment in `notes/environment.md`.
6. Record upstream commit hash.
7. Do not modify upstream artifact source.
8. Update the roadmap Status section.
9. Stop at M0 and report what you found, commands, files, concerns, and GO/NO-GO for M1.

Do not broaden scope.

[PASTE THE ROADMAP ABOVE HERE]

## PROMPT 1 — M1

Minimalism rule for this milestone:
- write the smallest code that works;
- do not add abstractions, comments, checks, files, or logs unless directly required;
- avoid LLM-style comments and boilerplate;
- prefer editing one existing file over creating several new ones;
- keep milestone reports short.


Read `PROJECT_ROADMAP.md` and existing notes. Execute M1 only.

Prove repeatedly on a disk-backed file that read/populate => cached and Flush => uncached, verified with `monitor-preadv2`.

Requirements:
- upstream artifact unchanged;
- exact artifact conventions where possible;
- raw output under `data/raw/`;
- >=3 repetitions;
- explicitly verify target path is not tmpfs;
- update `notes/artifact-reproduction.md` and roadmap Status.

Stop after M1 and report exact commands, state transitions, output files, GO/NO-GO for M2.

## PROMPT 2 — M2

Minimalism rule for this milestone:
- write the smallest code that works;
- do not add abstractions, comments, checks, files, or logs unless directly required;
- avoid LLM-style comments and boilerplate;
- prefer editing one existing file over creating several new ones;
- keep milestone reports short.


Read roadmap and notes. Execute M2 only.

Verify one Unix user/process can cause target page-cache state that another attacker user observes via artifact Flush/Monitor.

Requirements:
- real second test user if practical;
- no ptrace/process-table/victim-memory signal;
- minimal sufficient file permissions;
- record ownership/permissions;
- repeat multiple times;
- save raw evidence;
- update roadmap.

Stop with GO/NO-GO for M3.

## PROMPT 3 — M3

Minimalism rule for this milestone:
- write the smallest code that works;
- do not add abstractions, comments, checks, files, or logs unless directly required;
- avoid LLM-style comments and boilerplate;
- prefer editing one existing file over creating several new ones;
- keep milestone reports short.


Read roadmap, notes, and upstream README "Detecting Programs". Execute M3 only.

Reproduce documented local `htop` detection:
- ensure no existing htop;
- resolve real binary;
- artifact Flush;
- `monitor-preadv2` loop;
- execute htop from victim side;
- observe residency;
- capture exact output;
- repeat >=2 times;
- note spurious detections.

Do not substitute process inspection.

Update notes/roadmap and stop.

## PROMPT 4 — M4

Minimalism rule for this milestone:
- write the smallest code that works;
- do not add abstractions, comments, checks, files, or logs unless directly required;
- avoid LLM-style comments and boilerplate;
- prefer editing one existing file over creating several new ones;
- keep milestone reports short.


Read roadmap. Execute M4 only.

Build a tiny controlled victim:
- disk-backed `target.bin`;
- known page-aligned target offset;
- EVENT=1 `pread`s exactly target page;
- EVENT=0 does not;
- interactive and automated modes;
- monotonic timestamps/trial IDs;
- independent ground truth;
- simple Makefile.

Do not implement mitigation or complicated attacker yet.

Verify cache effect manually, update docs/status, stop.

## PROMPT 5 — M5

Minimalism rule for this milestone:
- write the smallest code that works;
- do not add abstractions, comments, checks, files, or logs unless directly required;
- avoid LLM-style comments and boilerplate;
- prefer editing one existing file over creating several new ones;
- keep milestone reports short.


Read roadmap and use existing victim. Execute M5 only.

First detect victim using upstream Flush + `monitor-preadv2` directly.

Only after that works, implement a minimal attacker harness if needed for automation:
- Flush target page;
- synchronize trial;
- observation window;
- Monitor;
- log prediction;
- never read victim ground truth.

Produce a clean two-terminal demo, update roadmap, stop.

## PROMPT 6 — M6

Minimalism rule for this milestone:
- write the smallest code that works;
- do not add abstractions, comments, checks, files, or logs unless directly required;
- avoid LLM-style comments and boilerplate;
- prefer editing one existing file over creating several new ones;
- keep milestone reports short.


Read roadmap. Execute M6 only.

Automate randomized trials:
- N=200 initially;
- P(EVENT)=0.5;
- explicit trial IDs;
- separate victim and attacker CSVs;
- attacker never gets event labels;
- synchronization coordinates trial boundaries only;
- preserve raw logs;
- validate exactly one row per trial on both sides.

Prefer simple IPC/barrier over timestamp matching.

Update roadmap and stop.

## PROMPT 7 — M7

Minimalism rule for this milestone:
- write the smallest code that works;
- do not add abstractions, comments, checks, files, or logs unless directly required;
- avoid LLM-style comments and boilerplate;
- prefer editing one existing file over creating several new ones;
- keep milestone reports short.


Read roadmap and raw trials. Execute M7 only.

Create `experiments/analyze.py`:
- validate alignment;
- TP/TN/FP/FN;
- accuracy/precision/recall/F1/FPR/FNR;
- explicit zero-denominator handling;
- processed summaries;
- confusion matrix;
- short ground-truth vs detection timeline.

Do not fabricate/smooth results. Do not overwrite raw logs.

Update roadmap and stop with measured baseline.

## PROMPT 8 — M8

Minimalism rule for this milestone:
- write the smallest code that works;
- do not add abstractions, comments, checks, files, or logs unless directly required;
- avoid LLM-style comments and boilerplate;
- prefer editing one existing file over creating several new ones;
- keep milestone reports short.


Read roadmap and baseline scripts. Execute M8.

Sweep observation/polling interval. Start from roadmap candidates; adapt if host behaviour requires.

For each:
- fresh randomized trials;
- separate raw data;
- metrics;
- runtime/config;
- proper attack-state reset.

Generate F1 vs interval. Optional load test only after required sweep is complete.

Update roadmap and stop.

## PROMPT 9 — M9

Minimalism rule for this milestone:
- write the smallest code that works;
- do not add abstractions, comments, checks, files, or logs unless directly required;
- avoid LLM-style comments and boilerplate;
- prefer editing one existing file over creating several new ones;
- keep milestone reports short.


Read roadmap. Execute M9.

Implement/evaluate controlled mitigation.

Preferred:
`victim_direct.c` with `O_DIRECT` if supported/practical.

First inspect direct-I/O requirements/alignment. Then:
- aligned implementation;
- verify success;
- test page-cache signal;
- measure victim latency;
- repeat exact baseline benchmark.

Do not frame O_DIRECT as universal.

Fallback if it becomes a time sink:
victim uses a private file/inode so cache identity is not shared.

Generate before/after metrics + latency. Update roadmap and stop.

## PROMPT 10 — M10

Minimalism rule for this milestone:
- write the smallest code that works;
- do not add abstractions, comments, checks, files, or logs unless directly required;
- avoid LLM-style comments and boilerplate;
- prefer editing one existing file over creating several new ones;
- keep milestone reports short.


Read roadmap and freeze experiment behaviour. Execute M10.

Create presentation-friendly:
- `demo/baseline.sh`
- `demo/mitigated.sh`

Make output clear, prerequisites explicit, no hidden ground-truth access.

Update README with prerequisites/build/demo/experiment/analysis/mitigation/limitations.

Prepare backup demo recording checklist. Update roadmap and stop.

## PROMPT 11 — Presentation assets

Minimalism rule for this milestone:
- write the smallest code that works;
- do not add abstractions, comments, checks, files, or logs unless directly required;
- avoid LLM-style comments and boilerplate;
- prefer editing one existing file over creating several new ones;
- keep milestone reports short.


Implementation and measurements are frozen.

Read roadmap, notes, processed results. Do not change attack behaviour unless fixing correctness.

Prepare:
- `notes/threat-model.md`;
- Mermaid/Graphviz attack architecture;
- Flush+Monitor timeline;
- setup table;
- confusion matrix;
- metrics table;
- interval plot;
- mitigation diagram;
- before/after table;
- limitations;
- evidence-backed conclusions.

Every numeric claim must point to generated processed data.
Stop with inventory of slide-ready assets.
