# flush-monitor

Small academic reproduction of the Flush+Monitor page-cache side channel from
the NDSS 2026 paper *Eviction Notice*. The project contains a one-page victim,
the original artifact as a pinned submodule, two Monitor cycles for current
Linux kernels, the measurement scripts and a constant-access mitigation.

The experiment asks one question: did the victim access a shared file page
during the observation window?

```text
Flush page -> victim may read -> Monitor page -> cached means EVENT
```

The attacker and victim use different Unix users and share read access to the
target inode.

The exam presentation is available as
[`presentation/flush-monitor.pdf`](presentation/flush-monitor.pdf), with the
editable version in [`presentation/flush-monitor.odp`](presentation/flush-monitor.odp).

## Layout

```text
artifact/       original Eviction Notice code
victim/         normal and constant-access victim
environment/    modern Monitor, sweep worker and fixed VM launcher
experiments/    acquisition and analysis scripts
data/           measured raw data and derived tables
demo/           local UI and cross-container demos
notes/          experiment record and interpretation
presentation/   exam slides in PDF and editable ODP format
```

## Build

```sh
git submodule update --init
make -C victim
```

The normal binary reads page 0 only for `EVENT=1`. The constant-access binary
performs the same `pread` for both events and only uses the byte for `EVENT=1`.

```sh
./victim/victim run victim/target.bin /tmp/truth.csv 1 0
./victim/victim-constant run victim/target.bin /tmp/truth-constant.csv 1 0
```

Guest binaries are static because the host and VM use different glibc versions:

```sh
make -C victim static-container
./environment/build-guest-tools.sh
```

## Cross-container demo

```sh
./demo/cross-container.sh
```

The script runs attacker and victim containers at the same time, under
different Unix users, sharing the same target.

Expected output:

```text
POLICY     EVENT   OBSERVATION
normal     0       miss
normal     1       hit
constant   0       hit
constant   1       hit
```

Containers share the host kernel and page cache, so a shared inode remains a
shared side channel. This demo uses the modern wait+Flush Monitor because the
host filesystem is Btrfs; `RWF_DONTCACHE` is not supported there.

The UI-redressing demo uses two small programs:

```sh
cc -O2 demo/ui-attacker.c -o /tmp/ui-attacker
cc -O2 demo/ui-victim.c -o /tmp/ui-victim
```

It requires Zenity. Run the attacker first and the victim in another terminal.

## Fixed VMs

The formal measurements use Ubuntu Jammy/ext4 guests with Linux
`5.15.0-187-generic` and `7.2.0-flushmon`. The repository does not download or
install operating systems. The fixed local images are described in
`environment/vm/README.md` and started with:

```sh
docker build -t eviction-notice-qemu:local environment/vm
./environment/vm/start.sh modern
```

## Experiments

The 50 ms randomized campaign runs inside a VM after copying the static
binaries, target and `experiments/run_trials.py` to `/tmp`:

```sh
sudo python3 /tmp/run_trials.py upstream 200 6101
sudo python3 /tmp/run_trials.py flush 200 6101
sudo python3 /tmp/run_trials.py dontcache 200 6101
```

Adding the victim policy produces the balanced before/after mitigation runs:

```sh
sudo python3 /tmp/run_trials.py flush 200 9109 normal
sudo python3 /tmp/run_trials.py flush 200 9109 constant
sudo python3 /tmp/run_trials.py dontcache 200 9109 normal
sudo python3 /tmp/run_trials.py dontcache 200 9109 constant
```

The observation-window sweep uses persistent workers pinned to the two guest
CPUs. With `window-sweep-worker` and `target.bin` in `/tmp`:

```sh
sudo SWEEP_OUTPUT=/tmp/sweep python3 /tmp/sweep_interval.py \
    flush 10000 8108 800 825 850 900
```

Regenerate the tables and plot from the stored raw data:

```sh
python3 experiments/analyze.py data/raw data/processed
python3 experiments/analyze_sweep.py data/raw data/processed plots
python3 experiments/analyze_mitigation.py data/raw data/processed
```

The warm victim benchmark is independent of attacker eviction:

```sh
python3 experiments/benchmark_victim.py TARGET NORMAL CONSTANT 100000 9109 RAW_DIR
```

## Results

- All three 50 ms baseline conditions classified 200/200 trials correctly.
- On Linux 7.2 the minimum confirmed window is 850 µs for wait+Flush and
  825 µs for `RWF_DONTCACHE`.
- Constant access changes the attack from one bit of mutual information to
  zero for both modern Monitors.
- The warm dummy read adds 380.73 ns to `EVENT=0`; `EVENT=1` is unchanged
  within measurement noise.

The compact outputs are `data/processed/baseline_metrics.csv`,
`m8-best-window.csv`, `m9-security-metrics.csv` and
`m9-victim-overhead.csv`. Methodology and caveats are in
`notes/experiment.md`.

## Limits

This is a controlled one-page local experiment, not a general exploit. The
modern Monitors restore the cache state only after transient contamination.
`RWF_DONTCACHE` depends on filesystem support, and constant access protects
only code paths whose observable page accesses can be equalized.
