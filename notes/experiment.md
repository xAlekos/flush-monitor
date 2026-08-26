# Experiment record

## Environment

The project was moved to Fedora 42 on 2026-08-24. The host has a 12th Gen Intel
Core i7-12700H, Linux `6.19.12-100.fc42.x86_64`, 4096-byte pages and a Btrfs
workspace. Docker and KVM are available.

Formal recent measurements use the fixed Ubuntu Jammy/ext4 modern guest with
two vCPUs and Linux `7.2.0-flushmon`. Older reference measurements use Ubuntu
Jammy/ext4 with Linux 5.15. VM runs expose the host CPU and use snapshot mode.

## Upstream reproduction

The artifact is pinned at commit
`84c57881855e23d47f7228f3c580d72111080a84`.

Its Flush opens the target read-only and calls `POSIX_FADV_DONTNEED`. Its
Monitor issues a one-byte `preadv2` with `RWF_NOWAIT`: a return value of 1 means
cached, while `EAGAIN` means the read would require I/O.

The basic tests produced 64 cached pages after Read and zero after Flush. A
second Unix user could repopulate the same pages, and the attacker observed the
change. Evidence is stored under `data/raw/read-flush-*` and
`cross-user-*`.

## Behaviour since Linux 6.12

The original Monitor is no longer non-contaminating on current upstream Linux.
After an `EAGAIN` miss, the page appears in cache asynchronously. The same
transition occurred on Btrfs and ext4.

Linux commit `0a2d82946be67e02fdf85a4010606bdc0546ba44` removed the implicit
`IOCB_NOIO` from userspace `RWF_NOWAIT`. Readahead may now be submitted even
though the call does not wait for it. The causal trace is
`data/raw/monitor-rwf-nowait-readahead.txt`.

The project therefore uses two state-restoring cycles on modern kernels:

1. wait for the asynchronous fill and Flush the page;
2. use `RWF_NOWAIT|RWF_DONTCACHE` until drop-behind removes the page.

Both classify the original state before contamination. The second is slightly
faster on ext4 but Btrfs rejects `RWF_DONTCACHE` with `EOPNOTSUPP`.

The earlier isolated timing campaign used 10,000 hits and 10,000 misses:

| state | Monitor | full-cycle p50 |
|---|---|---:|
| miss | reference 5.15 | 0.232 µs |
| miss | modern wait+Flush | 13.689 µs |
| miss | modern `RWF_DONTCACHE` | 13.169 µs |
| hit | reference 5.15 | 0.777 µs |
| hit | modern wait+Flush | 0.814 µs |
| hit | modern `RWF_DONTCACHE` | 0.820 µs |

Raw tables are under `data/raw/monitor-timing-*`.

## Controlled attack

The victim owns a one-page target readable by the attacker. `EVENT=0` performs
no read; `EVENT=1` reads page 0. The victim event log is joined with the
attacker output during analysis.

The original 5.15 Monitor and both modern cycles classified all 200 randomized
50 ms trials correctly. Each condition used the same seeded event sequence.
The processed result is `data/processed/baseline_metrics.csv`.

## Observation window

The sweep uses persistent attacker, victim and validator processes. Attacker
and victim are pinned to different vCPUs and follow absolute monotonic
deadlines. The validator records the page state before the event and after
cleanup.

A window is confirmed with 10,000 trials when accuracy and recall are at least
99%, false-positive, cleanup-failure and timing-inversion rates are at most 1%,
and the corresponding 95% Wilson bounds meet the same limits.

| Monitor | lower rejected | minimum confirmed | accuracy | recall | FPR |
|---|---:|---:|---:|---:|---:|
| wait+Flush | 825 µs | 850 µs | 99.86% | 99.718% | 0% |
| `RWF_DONTCACHE` | 800 µs | 825 µs | 99.66% | 99.316% | 0% |

These are end-to-end VM timings including victim I/O and scheduling. They are
not directly comparable with the paper's approximately 0.8 µs Flush-limited
primitive timing. Full results are in `data/processed/window-sweep-*`.

The one-page target stays in `victim/` and `make clean` leaves it untouched.

## Constant-access mitigation

The mitigated victim reads the monitored page for both event values. Only
`EVENT=1` uses the byte, but page-cache residency is identical for `EVENT=0`
and `EVENT=1`.

Each before/after condition used 200 balanced trials on Linux 7.2:

| victim | Monitor | accuracy | TPR-FPR | mutual information |
|---|---|---:|---:|---:|
| normal | wait+Flush | 100% | 1 | 1 bit |
| normal | `RWF_DONTCACHE` | 100% | 1 | 1 bit |
| constant | wait+Flush | 50% | 0 | 0 bit |
| constant | `RWF_DONTCACHE` | 50% | 0 | 0 bit |

The 50% figure alone is not the security result: the important observation is
that the prediction is constant and contains no information about the event.

A separate warm-cache benchmark measured 100,000 operations per build. The
dummy read adds 380.73 ns to `EVENT=0`; `EVENT=1` differs by about 2%.
With balanced events the mean overhead is 185.78 ns per operation.

This mitigation addresses only the controlled event-dependent access. It is
not a general defense against page-cache side channels.
