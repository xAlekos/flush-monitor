#!/usr/bin/env python3
import os
from pathlib import Path
import random
import subprocess
import sys
import time


ATTACKER = "fm_attacker"
VICTIM = "fm_victim"
WINDOW_NS = 50_000_000


def run(command, user=None, output=False):
    if user:
        command = ["sudo", "-u", user, *command]
    return subprocess.run(command, check=True, text=True,
                          stdout=subprocess.PIPE if output else subprocess.DEVNULL)


def make_user(name):
    if subprocess.run(["id", "-u", name], stdout=subprocess.DEVNULL,
                      stderr=subprocess.DEVNULL).returncode:
        run(["useradd", "--system", "--no-create-home", name])


def classify(primitive, text):
    if primitive == "upstream":
        line = next(line for line in text.splitlines() if line.startswith("0000,"))
        return int(line.split(",")[1])
    return int("classification=hit" in text)


def main():
    if len(sys.argv) not in (4, 5):
        sys.exit(f"usage: {sys.argv[0]} upstream|flush|dontcache COUNT SEED "
                 "[normal|constant]")
    primitive, count, seed = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    policy = sys.argv[4] if len(sys.argv) == 5 else "normal"
    campaign = "m9" if len(sys.argv) == 5 else "m6"
    name = (f"{campaign}-{policy}-{primitive}" if campaign == "m9"
            else f"m6-{primitive}")
    directory = Path("/var/tmp") / name
    target = directory / "target.bin"
    truth = directory / "ground-truth.csv"
    attacker_log = directory / "attacker.csv"
    victim = ("/tmp/victim-constant-static" if policy == "constant"
              else "/tmp/victim-static")

    make_user(ATTACKER)
    make_user(VICTIM)
    run(["install", "-d", "-m", "0755", "-o", VICTIM, "-g", VICTIM,
         directory])
    run(["install", "-m", "0644", "-o", VICTIM, "-g", VICTIM,
         "/tmp/target.bin", target])
    run(["sync", "-f", target])
    truth.unlink(missing_ok=True)
    run(["install", "-m", "0644", "-o", ATTACKER, "-g", ATTACKER,
         "/dev/null", attacker_log])
    log = attacker_log.open("w")
    log.write("trial_id,timestamp_ns,prediction\n")

    randomizer = random.Random(seed)
    if campaign == "m9":
        events = [i % 2 for i in range(count)]
        randomizer.shuffle(events)
    else:
        events = [randomizer.getrandbits(1) for _ in range(count)]

    for trial, event in enumerate(events, 1):
        run(["/tmp/flush", target, "0"], ATTACKER)
        deadline = time.monotonic_ns() + WINDOW_NS
        run([victim, "run", target, truth, str(trial), str(event)], VICTIM)
        time.sleep(max(0, deadline - time.monotonic_ns()) / 1e9)
        if primitive == "upstream":
            monitor = ["/tmp/monitor-preadv2", target, "0"]
        else:
            monitor = ["/tmp/monitor-modern-once", primitive, target]
        prediction = classify(primitive, run(monitor, ATTACKER, True).stdout)
        log.write(f"{trial},{time.monotonic_ns()},{prediction}\n")
    log.close()

    prefix = Path("/tmp") / name
    run(["install", "-m", "0644", attacker_log, f"{prefix}-attacker.csv"])
    run(["install", "-m", "0644", truth, f"{prefix}-ground-truth.csv"])
    Path(f"{prefix}-setup.txt").write_text(
        f"primitive={primitive}\naccess_policy={policy}\n"
        f"kernel={os.uname().release}\ntrials={count}\nseed={seed}\n"
        f"observation_window_ns={WINDOW_NS}\n"
    )
    print(primitive, policy, f"trials={count}")


if __name__ == "__main__":
    main()
