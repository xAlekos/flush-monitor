# Artifact reproduction

Upstream artifact: `https://github.com/isec-tugraz/Eviction-Notice.git`

- Local path: `artifact/Eviction-Notice`
- Commit: `84c57881855e23d47f7228f3c580d72111080a84`
- Upstream worktree after inspection: clean

Only the upstream README sections Requirements, Flush, Monitor, and Detecting
Programs were read. Source inspection was limited to the Flush and
`monitor-preadv2` build paths.

## Primitive logic

Flush opens the target read-only and calls `posix_fadvise` with
`POSIX_FADV_DONTNEED` for one page-aligned range at a time. Its timing output is
used to report whether a page appeared cached before the call; the configured
upstream threshold is 900 ns. The call itself drops the clean cached page.

Monitor issues a one-byte `preadv2` at the selected page offset with
`RWF_NOWAIT`. A return value of 1 means the byte was already available in the
page cache. `-1` with `errno == EAGAIN` means the read would require I/O, so the
page is reported as not cached. Thus the probe does not load an absent page.
The whole-file monitor visits pages in reverse order and prints both per-page
state and the cached-page total.

The documented program-detection PoC flushes the pages of the `htop` binary,
polls them with `monitor-preadv2`, and infers execution when cached pages appear.
That PoC belongs to M3 and has not been run yet.

## M1 result

The upstream `read`, `flush`, and `monitor-preadv2` targets built successfully.
A 64-page, disk-backed test file was exercised three times. In every run,
Monitor reported 64 cached pages after Read and 0 cached pages after Flush. All
commands exited successfully; full transcripts are in `data/raw/m1-run-*.txt`.

## M2 result

Cross-UID observation was tested with the attacker as UID 1000 and the victim
read as UID 65534 in a temporary, network-disabled container. The container was
used only to obtain a distinct UID without creating a system account; it used
the host kernel and the same Btrfs inode through a read-only bind mount. The
target was owned by UID 1000 and readable with mode `0644`.

Across three runs, the attacker observed 0 of 64 pages cached immediately after
Flush and 64 of 64 after the other UID read the target. All commands exited
successfully. Identity and filesystem details are in `data/raw/m2-setup.txt`;
run transcripts are in `data/raw/m2-run-*.txt`.

## M3 result

The local `/usr/bin/htop` target was detected twice with the upstream Flush and
`monitor-preadv2` primitives. In each controlled run, the first Monitor after an
idle observation window reported 0 of 94 pages cached; after a fresh Flush and
launching `htop`, the first Monitor reported 94 of 94 pages cached. The exact
outputs are in `data/raw/m3-controlled-run-*.txt`.

The README's continuous Monitor loop is not stable on the host kernel.
With no victim, repeated `preadv2` probes produced `0 -> 1` for a single page
and `0 -> 94 -> 94` for the whole `htop` binary: the first miss appears to
populate the cache asynchronously. Repeated `cachestat` controls remained
`0 -> 0 -> 0`. M3 therefore uses one Monitor per fresh Flush and records the
loop incompatibility in `data/raw/m3-idle-diagnostic.txt` and
`data/raw/m3-cachestat-diagnostic.txt`. A loop-mounted ext4 test reproduced the
same contamination, while an Ubuntu 22.04 VM with kernel 5.15 and ext4 did not.
The cause is upstream commit `0a2d82946be6`, included from Linux 6.12: it removed
the implicit `IOCB_NOIO` from `RWF_NOWAIT` and intentionally allowed the
non-blocking read to submit readahead. Detailed source comparison and the local
causal test are recorded in `notes/environment.md`.

## M3A result

The reference Monitor and both modern recovery cycles were compared in matched
Ubuntu/ext4/KVM guests, with only the guest kernel condition changed. Across
10,000 hits and 10,000 misses per mechanism, every classification and cleanup
validation succeeded. Modern miss recovery reduced the p50 theoretical rate
from 4.31 MHz to 73.1 kHz for wait+Flush and 75.9 kHz for
`RWF_NOWAIT|RWF_DONTCACHE`; hit cycles remained near the reference result.

Both modern variants also passed six cross-UID trials each: three misses and
three victim-generated hits. The full table, caveats, raw CSVs, and exact guest
manifests are recorded in `notes/environment.md` and `data/raw/m3a-*`.

## M5 result

The controlled victim was tested with all three Monitor conditions in the
matched VMs. The unmodified upstream `monitor-preadv2` on kernel
5.15.0-1105-kvm classified 4/4 trials correctly. On kernel 7.0.3-arch1-2,
wait+Flush classified 4/4 and `RWF_NOWAIT|RWF_DONTCACHE` classified 4/4. Each
set contained two EVENT and two NO-EVENT trials.

The attacker and victim ran as UID 997 and UID 996. The target was owned by the
victim and readable with mode `0644`; victim ground truth was mode `0600`, and
an explicit attacker-side read check failed in both guests. Attacker transcripts
contain no event labels and were compared with ground truth only after the
trials ended.

A newly copied target is initially dirty, so the runner calls `sync -f` once
before the first Flush. Without that writeback, `POSIX_FADV_DONTNEED` may leave
the dirty page cached and create a first-trial false hit. Evidence is in
`data/raw/m5-{reference,modern}-{attacker,ground-truth,setup}.*`; the minimal
runner is `environment/run-m5-guest.sh`.

## M6 result

The three conditions each completed 200 randomized trials with `P(EVENT)=0.5`,
the same seed and therefore the same event sequence. A 50 ms monotonic deadline
formed the trial barrier; the runner aborts if victim execution exceeds it.
Only the event-independent end of that window starts the attacker Monitor.

For every condition, both CSVs contain exactly one row for each trial ID from 1
through 200. The attacker CSV contains only timestamp, prediction, and Monitor
result; the separate victim CSV contains the event. Attacker UID 997 could not
read the victim-owned mode `0600` ground truth during any batch. No prediction
metrics were computed in M6.

The runner is `experiments/run_trials.py`. Raw CSVs and manifests are
`data/raw/m6-{upstream,flush,dontcache}-*`; the reference used kernel 5.15 and
the two modern conditions used kernel 7.0.

## M7 result

`experiments/analyze.py` joins attacker and ground-truth rows by trial ID,
rejects duplicates, invalid bits, missing IDs, and differing event sequences,
then computes the baseline with explicit `NA` handling for zero denominators.
One command regenerates all processed outputs from raw data.

All three primitives measured TP=103, TN=97, FP=0, and FN=0. Accuracy,
precision, recall, and F1 are therefore 1.0; FPR and FNR are 0.0. These values
describe the controlled one-page victim with a 50 ms window, not arbitrary
programs or system load.

Outputs are `data/processed/baseline_metrics.csv`,
`baseline_confusion_matrix.csv`, and the first-20-trial comparison in
`baseline_timeline.csv`.
