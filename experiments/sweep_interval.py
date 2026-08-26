#!/usr/bin/env python3
import csv
import os
from pathlib import Path
import platform
import random
import subprocess
import sys
import time


ATTACKER = "fm_attacker"
VICTIM = "fm_victim"
WORKER = "/tmp/window-sweep-worker"
TARGET = Path("/var/tmp/flush-monitor/target.bin")
OUTPUT = Path(os.environ.get("SWEEP_OUTPUT", "/tmp/window-sweep"))
LEAD = 2_000_000
FLUSH_GUARD = 2_000_000
CHECK_LEAD = 1_000_000

FIELDS = (
    "mode", "kernel", "interval_us", "trial_id", "event", "prediction",
    "flush_start_ns", "flush_end_ns", "event_start_ns", "monitor_start_ns",
    "cleanup_end_ns", "baseline_check_start_ns", "baseline_check_end_ns",
    "baseline_cached", "cleanup_cached",
    "attacker_error", "victim_error", "validator_error",
)


def run(command):
    return subprocess.run(command, check=True, text=True,
                          stdout=subprocess.DEVNULL)


def make_user(name):
    if subprocess.run(["id", "-u", name], stdout=subprocess.DEVNULL,
                      stderr=subprocess.DEVNULL).returncode:
        run(["useradd", "--system", "--no-create-home", name])


def start(role, mode):
    if role == "attacker":
        command = ["taskset", "-c", "0", WORKER, role, mode, TARGET]
        user = ATTACKER
    elif role == "victim":
        command = ["taskset", "-c", "1", WORKER, role, TARGET]
        user = VICTIM
    else:
        command = ["taskset", "-c", "0", WORKER, role, TARGET]
        user = VICTIM
    if user:
        command = ["sudo", "-u", user, *command]
    process = subprocess.Popen(command, text=True, stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    process.stdout.readline()
    return process


def numbers(process):
    return [int(value) for value in process.stdout.readline().strip().split(",")]


def validator_row(process):
    return [int(value) for value in
            process.stdout.readline().strip().split(",")[1:]]


def stop(process):
    process.stdin.close()
    process.wait()


def measure(mode, interval, events, kernel):
    attacker = start("attacker", mode)
    victim = start("victim", mode)
    validator = start("validator", mode)
    raw = OUTPUT / f"window-sweep-{mode}-{interval}us.csv"

    try:
        with raw.open("w", newline="") as file:
            writer = csv.writer(file)
            writer.writerow(FIELDS)
            for trial, event in enumerate(events, 1):
                flush_at = time.monotonic_ns() + LEAD
                event_at = flush_at + FLUSH_GUARD
                monitor_at = event_at + interval * 1000
                check_at = event_at - CHECK_LEAD

                validator.stdin.write(f"B {trial} {check_at}\n")
                victim.stdin.write(f"{trial} {event} {event_at}\n")
                attacker.stdin.write(f"{trial} {flush_at} {monitor_at}\n")
                validator.stdin.flush()
                victim.stdin.flush()
                attacker.stdin.flush()

                baseline = validator_row(validator)
                victim_row = numbers(victim)
                attacker_row = numbers(attacker)
                validator.stdin.write(f"C {trial} 0\n")
                validator.stdin.flush()
                cleanup = validator_row(validator)

                writer.writerow((
                    mode, kernel, interval, trial, event, attacker_row[4],
                    attacker_row[1], attacker_row[2], victim_row[2],
                    attacker_row[3], attacker_row[5], baseline[1], baseline[2],
                    baseline[3], cleanup[3], attacker_row[6], victim_row[3],
                    baseline[4] or cleanup[4],
                ))
    finally:
        stop(validator)
        stop(victim)
        stop(attacker)


def main():
    if len(sys.argv) < 5:
        sys.exit(f"usage: {sys.argv[0]} flush|dontcache COUNT SEED INTERVAL...")
    mode, count, seed = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    intervals = sorted(map(int, sys.argv[4:]))
    make_user(ATTACKER)
    make_user(VICTIM)
    run(["install", "-d", "-m", "0755", "-o", VICTIM, "-g", VICTIM,
         TARGET.parent])
    run(["install", "-m", "0644", "-o", VICTIM, "-g", VICTIM,
         "/tmp/target.bin", TARGET])
    run(["sync", "-f", TARGET])
    OUTPUT.mkdir(parents=True, exist_ok=True)

    randomizer = random.Random(seed)
    events = [randomizer.getrandbits(1) for _ in range(count)]
    kernel = platform.release()
    for interval in intervals:
        measure(mode, interval, events, kernel)
        print(mode, f"window={interval} us", f"trials={count}")

    setup = OUTPUT / (f"window-sweep-{mode}-setup-"
                      f"{intervals[0]}-{intervals[-1]}us.txt")
    setup.write_text(
        f"mode={mode}\nkernel={kernel}\ntrials_per_interval={count}\n"
        f"seed={seed}\nintervals_us={','.join(map(str, intervals))}\n"
        "attacker_cpu=0\nvictim_cpu=1\n"
    )


if __name__ == "__main__":
    main()
