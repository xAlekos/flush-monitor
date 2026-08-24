# Environment

Collected on 2026-08-23.

- Host: Arch Linux, kernel `7.0.3-arch1-2`, x86_64.
- CPU: 11th Gen Intel Core i9-11900F, 8 cores / 16 threads.
- Page size: 4096 bytes.
- glibc: 2.43.
- GCC: 16.1.1.
- GNU Make: 4.4.1.
- Root filesystem: Btrfs on `/dev/nvme0n1p2`.
- Workspace filesystem: read-write Btrfs on `/dev/nvme0n1p2`.
- `/tmp`: tmpfs; do not use it for page-cache measurements.
- Disk-backed target directory: `/home/bantinus/Progetti/evictionNotice`.

The kernel and glibc satisfy the upstream minimums for the primary monitor
primitive: kernel newer than 4.14 and glibc at least 2.34. Upstream tested the
mechanisms on Btrfs, although its main evaluation used Debian-based systems and
ext4. This host is therefore compatible on paper, with Btrfs behaviour still to
be verified experimentally. M1 subsequently confirmed the Flush and
`preadv2(..., RWF_NOWAIT)` primitives on this Btrfs workspace.

Repeated Monitor probes later exposed a host incompatibility: after a miss, the
first `preadv2` probe causes the page to appear cached to subsequent probes.
The same `0 -> 1 -> 1 -> 1` result occurs on a loop-mounted ext4 filesystem, so
changing only the filesystem does not remove it. The transcript is
`data/raw/environment-host-ext4-monitor.txt`.

The compatible environment is an Ubuntu 22.04.5 VM with kernel
`5.15.0-1105-kvm` and an ext4 root filesystem. There, four repeated single-page
probes remained `0 -> 0 -> 0 -> 0`, and three repeated whole-file probes
remained `0 -> 0 -> 0` for all 64 pages. See
`data/raw/environment-vm-monitor*.txt` and `environment/vm/README.md`.

## Cause of the Monitor contamination

The behaviour changes at upstream Linux 6.12. Before that release,
`kiocb_set_rw_flags()` made userspace `RWF_NOWAIT` set both `IOCB_NOWAIT` and
`IOCB_NOIO`. `IOCB_NOIO` stops the generic buffered-read path before it can
submit readahead, which is why the 5.15 VM leaves a missing page absent.

Commit `0a2d82946be67e02fdf85a4010606bdc0546ba44`, first present in Linux 6.12,
removed the implicit `IOCB_NOIO`. It intentionally restored non-blocking
readahead for `RWF_NOWAIT`: the call may submit page-cache I/O but still returns
`EAGAIN` instead of waiting for it. The 7.0 path calls `page_cache_sync_ra()`
under `memalloc_noio_save()` so allocation/reclaim does not turn the operation
into a blocking read.

The local causal check was:

```text
cachestat before       0
preadv2(RWF_NOWAIT)    EAGAIN (artifact reports 0)
cachestat after        1
```

No victim access occurred. The transcript is
`data/raw/monitor-rwf-nowait-readahead.txt`. This confirms that the first
Monitor miss itself schedules the transition into cache; it is not a Btrfs
effect or merely a wrong second-probe classification.

For this project, kernels through 6.11 are the expected compatibility range for
the artifact's non-contaminating `preadv2` Monitor. Kernels 6.12 and newer must
be assumed contaminating unless a distribution has changed this path. The
paper artifact's lower bound of kernel 4.14 is therefore insufficient for the
continuous Monitor loop on current upstream kernels.

## Alternative Monitor candidate on the host

On Linux 7.0 with ext4, combining `RWF_NOWAIT` with `RWF_DONTCACHE` provides a
state-restoring approximation. The first return classifies the original state.
After an uncached result, a second call made when the asynchronous read has
completed consumes the folio marked for drop-behind and removes it again. An
initially cached folio remains cached.

This is not equivalent to the paper's non-contaminating Monitor: on a miss the
page is transiently present and disk I/O is generated. It therefore has a race
window and must be evaluated separately. It is also filesystem-dependent;
Btrfs returns `EOPNOTSUPP`, while the local ext4 test succeeded. The exact
transcript and validation probe are
`data/raw/monitor-rwf-nowait-dontcache-ext4.txt` and
`environment/probe-rwf-dontcache.c`.

The first timing characterization used 1000 repeated misses on loop-mounted
ext4. Median classification took 1.81 us, restoration after that result took
2.30 us, and the Monitor-to-restored upper-bound window was 4.13 us. Including
the leading Flush, the median sequence was 4.27 us. A robust plain
`RWF_NOWAIT` Monitor followed by waiting for I/O and then Flush took 4.63 us
median, so `RWF_DONTCACHE` did not reduce best-case sampling resolution in this
comparison; it was about 0.36 us faster.

A Flush issued immediately after the first `EAGAIN` is not equivalent to that
robust comparison. In 1000 trials, 974 pages were cached after 50 us and the
other 26 appeared by 1 ms: the Flush had raced ahead of the asynchronous fill.
Full results and the benchmark source are
`data/raw/monitor-rwf-dontcache-window-benchmark.txt` and
`environment/benchmark-monitor-window.c`. These are warm-backing best-case
numbers; real storage and scheduler contention determine the long tail.

## Matched-VM temporal comparison (M3A)

The formal comparison used two copy-on-write guests from the same Ubuntu
22.04.5 base image. Both had KVM host CPU exposure, 2 vCPU, 2 GiB configured
RAM, a 4 GiB virtio disk, ext4, `kvm-clock`, the same one-page target and the
same static benchmark binary. The reference guest ran `5.15.0-1105-kvm`; the
modern guest ran `7.0.3-arch1-2`. The modern kernel came from the host Arch
package, so this is a comparison of kernel conditions rather than a pure
single-patch syscall benchmark.

Each mechanism received 10,000 hit and 10,000 miss samples pinned to guest CPU
0. State preparation and `mincore` validation were outside the timed interval.
The full cycle begins with Monitor classification and ends at the common absent
baseline. A hit therefore includes a final Flush for all three mechanisms. On
a miss, the reference needs no cleanup, the robust modern variant waits for
submitted I/O and Flushes, and the candidate repeats
`RWF_NOWAIT|RWF_DONTCACHE` until drop-behind completes.

| State | Mechanism | Classify p50 | Cleanup p50 | Full p50 | Full p95 | Full p99 | p50 rate | Failures |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| miss | reference 5.15 | 0.214 us | 0.018 us | 0.232 us | 0.241 us | 0.361 us | 4.31 MHz | 0/10,000 |
| miss | modern wait + Flush | 2.460 us | 11.207 us | 13.689 us | 18.806 us | 28.426 us | 73.1 kHz | 0/10,000 |
| miss | modern `RWF_DONTCACHE` | 2.453 us | 10.705 us | 13.169 us | 18.718 us | 24.589 us | 75.9 kHz | 0/10,000 |
| hit | reference 5.15 | 0.286 us | 0.491 us | 0.777 us | 0.985 us | 1.637 us | 1.287 MHz | 0/10,000 |
| hit | modern wait + Flush | 0.259 us | 0.555 us | 0.814 us | 1.186 us | 2.033 us | 1.229 MHz | 0/10,000 |
| hit | modern `RWF_DONTCACHE` | 0.259 us | 0.561 us | 0.820 us | 1.201 us | 2.130 us | 1.220 MHz | 0/10,000 |

For misses, the modern full-cycle median is 59.0x the reference with explicit
wait+Flush and 56.8x with `RWF_DONTCACHE`. The candidate is 3.8% faster than
wait+Flush at p50 and 13.5% faster at p99 in this warm-backing run. Hit cycles
remain within about 6% at p50. Reported rates are reciprocals of measured cycle
latency and are theoretical upper bounds, not sustainable end-to-end attack
rates.

The cross-UID test used attacker UID 997 and victim UID 996 on a victim-owned,
world-readable one-page inode. Each modern variant passed three hit and three
miss trials. The attacker classified the victim read and restored the absent
baseline using only read-open, Flush and Monitor operations; the victim-side
`mincore` process was independent validation. All immediate and 50 ms cleanup
checks were absent. This confirms the same privilege-level threat model for
both modern variants in the tested ext4 environment. It does not remove the
candidate's filesystem-support restriction or either variant's transient miss
contamination.

Raw data and exact manifests:

- `data/raw/m3a-reference-kernel-5.15-{summary.txt,raw.csv}`
- `data/raw/m3a-modern-flush-kernel-7.0-{summary.txt,raw.csv}`
- `data/raw/m3a-modern-dontcache-kernel-7.0-{summary.txt,raw.csv}`
- `data/raw/m3a-{reference,modern}-environment.txt`
- `data/raw/m3a-crossuid-modern-kernel-7.0.txt`

The benchmark and cross-UID harness are
`environment/benchmark-monitor-resolution.c` and
`environment/run-crossuid-modern.sh`.

Primary references:

- Linux commit [`0a2d82946be6`](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=0a2d82946be67e02fdf85a4010606bdc0546ba44)
- Linux documentation for [`IOCB_NOWAIT` and `IOCB_NOIO`](https://docs.kernel.org/6.5/core-api/mm-api.html)

Commands used:

```sh
uname -a
uname -r
cat /etc/os-release
ldd --version
getconf PAGESIZE
findmnt /
findmnt -T /tmp
findmnt -T /home/bantinus/Progetti/evictionNotice
stat -f -c '%T' .
gcc --version
make --version
lscpu
```
