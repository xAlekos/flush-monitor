# flush-monitor

## Controlled victim

Build the M4 victim and its one-page disk-backed target:

```sh
make -C victim
```

Run one automated trial:

```sh
./victim/victim run victim/target.bin victim/ground-truth.csv 1 0
./victim/victim run victim/target.bin victim/ground-truth.csv 2 1
```

Or use the interactive demo:

```sh
./victim/victim interactive victim/target.bin victim/ground-truth.csv
```

`EVENT=1` reads exactly page 0 with one page-sized `pread`; `EVENT=0` does not
read the target. Ground truth is written separately with mode `0600` as
`trial_id,timestamp_ns,target_page,event`.

M4 validation evidence is in `data/raw/m4-cache-validation.txt` and
`data/raw/m4-ground-truth.csv`.

## Controlled detection

M5 uses the same static victim and one-page target in the matched VMs:

```sh
make -C victim static
./environment/vm/start.sh reference   # upstream Monitor on kernel 5.15
./environment/vm/start.sh modern      # both modern Monitors on kernel 7.0
```

`environment/run-m5-guest.sh` is the guest-side reproducibility runner.  For a
two-terminal trial after provisioning `/var/tmp/m5`, run Flush in the attacker
terminal, one victim action in the other terminal, then the appropriate
Monitor:

```sh
# attacker
sudo -u m5_attacker /tmp/flush /var/tmp/m5/target.bin 0

# victim: final argument 0 is NO-EVENT, 1 is EVENT
sudo -u m5_victim /tmp/victim-static run /var/tmp/m5/target.bin \
  /var/tmp/m5/ground-truth.csv 1 1

# attacker, reference kernel
sudo -u m5_attacker /tmp/monitor-preadv2 /var/tmp/m5/target.bin 0

# attacker, modern kernel: select one
sudo -u m5_attacker /tmp/monitor-modern-once flush /var/tmp/m5/target.bin
sudo -u m5_attacker /tmp/monitor-modern-once dontcache /var/tmp/m5/target.bin
```

The reference primitive and both modern alternatives each classified two
EVENT and two NO-EVENT trials correctly. Raw attacker output, separate victim
ground truth, and access-control manifests are in `data/raw/m5-*`.

## Randomized trials

M6 is driven inside each guest by:

```sh
sudo python3 /tmp/run_trials.py upstream 200 6101
sudo python3 /tmp/run_trials.py flush 200 6101
sudo python3 /tmp/run_trials.py dontcache 200 6101
```

The runner is `experiments/run_trials.py`. All conditions use the same 200-bit
random sequence and a verified 50 ms observation barrier. Each produces a
separate attacker CSV and victim ground-truth CSV under `data/raw/m6-*`.

## Baseline metrics

Reproduce all M7 processed outputs from the raw M6 data with:

```sh
python3 experiments/analyze.py data/raw data/processed
```

In the controlled baseline, each primitive produced TP=103, TN=97, FP=0 and
FN=0: accuracy, precision, recall and F1 are 1.0, while FPR and FNR are 0.0.
See `data/processed/baseline_metrics.csv`,
`baseline_confusion_matrix.csv`, and `baseline_timeline.csv`.
