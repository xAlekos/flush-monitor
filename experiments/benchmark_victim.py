#!/usr/bin/env python3
import csv
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile


def measure(policy, binary, target, events, directory):
    log = directory / f"{policy}.csv"
    target.open("rb").read(os.sysconf("SC_PAGE_SIZE"))
    commands = "".join(f"{event}\n" for event in events) + "q\n"
    subprocess.run(
        [binary, "interactive", target, log],
        input=commands,
        text=True,
        stdout=subprocess.DEVNULL,
        check=True,
    )
    with log.open(newline="") as source:
        return list(csv.DictReader(source))


def main():
    if len(sys.argv) != 7:
        sys.exit(
            f"usage: {sys.argv[0]} TARGET NORMAL CONSTANT COUNT SEED RAW_DIR"
        )

    target, normal, constant = map(Path, sys.argv[1:4])
    count, seed = map(int, sys.argv[4:6])
    output = Path(sys.argv[6])
    output.mkdir(parents=True, exist_ok=True)

    events = [i % 2 for i in range(count)]
    random.Random(seed).shuffle(events)
    binaries = {"normal": normal, "constant": constant}

    with tempfile.TemporaryDirectory() as name:
        directory = Path(name)
        rows = {
            policy: measure(policy, binary, target, events, directory)
            for policy, binary in binaries.items()
        }

    raw = output / "mitigation-victim-performance-raw.csv"
    with raw.open("w", newline="") as file:
        writer = csv.writer(file)
        writer.writerow(("policy", "trial_id", "event", "duration_ns"))
        for policy in binaries:
            for row in rows[policy]:
                writer.writerow((
                    policy, row["trial_id"], row["event"], row["duration_ns"]
                ))

    (output / "mitigation-victim-performance-setup.txt").write_text(
        f"kernel={os.uname().release}\n"
        f"trials_per_policy={count}\n"
        f"seed={seed}\n"
        "events=exactly_balanced\n"
        "cache_state=warm\n"
    )
    print(raw)


if __name__ == "__main__":
    main()
