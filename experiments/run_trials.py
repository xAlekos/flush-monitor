#!/usr/bin/env python3
import os
import random
import re
import subprocess
import sys
import time


ATTACKER = "m6_attacker"
VICTIM = "m6_victim"
WINDOW_NS = 50_000_000


def run(cmd, user=None, capture=False):
    if user:
        cmd = ["sudo", "-u", user, *cmd]
    return subprocess.run(
        cmd,
        check=True,
        text=True,
        stdout=subprocess.PIPE if capture else subprocess.DEVNULL,
    )


def account(name):
    if subprocess.run(["id", "-u", name], stdout=subprocess.DEVNULL,
                      stderr=subprocess.DEVNULL).returncode:
        run(["useradd", "--system", "--no-create-home", "--shell",
             "/usr/sbin/nologin", name])


def prediction(primitive, output):
    if primitive == "upstream":
        rows = [line for line in output.splitlines() if line.startswith("0000,")]
        if len(rows) != 1:
            raise RuntimeError("invalid upstream Monitor output")
        value = int(rows[0].split(",")[1])
        if value not in (0, 1):
            raise RuntimeError("invalid upstream classification")
        return value, "cached" if value else "not_cached"

    match = re.search(r"classification=(hit|miss)", output)
    if not match:
        raise RuntimeError("invalid modern Monitor output")
    value = int(match.group(1) == "hit")
    return value, "cached" if value else "not_cached"


def main():
    if len(sys.argv) != 4 or sys.argv[1] not in {"upstream", "flush", "dontcache"}:
        sys.exit(f"usage: {sys.argv[0]} upstream|flush|dontcache COUNT SEED")
    if os.geteuid() != 0:
        sys.exit("run as root")

    primitive = sys.argv[1]
    count = int(sys.argv[2])
    seed = int(sys.argv[3])
    rng = random.Random(seed)
    work = f"/var/tmp/m6-{primitive}"
    target = f"{work}/target.bin"
    truth = f"{work}/ground-truth.csv"
    attacker_log = f"{work}/attacker.csv"
    prefix = f"/tmp/m6-{primitive}"

    account(ATTACKER)
    account(VICTIM)
    run(["install", "-d", "-o", VICTIM, "-g", VICTIM, "-m", "0755", work])
    run(["install", "-o", VICTIM, "-g", VICTIM, "-m", "0644",
         "/tmp/target.bin", target])
    run(["sync", "-f", target])
    for path in (truth, attacker_log):
        try:
            os.unlink(path)
        except FileNotFoundError:
            pass

    fd = os.open(attacker_log, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o644)
    os.fchown(fd, int(run(["id", "-u", ATTACKER], capture=True).stdout),
              int(run(["id", "-g", ATTACKER], capture=True).stdout))
    log = os.fdopen(fd, "w")
    log.write("trial_id,timestamp_ns,prediction,monitor_result\n")

    for trial in range(1, count + 1):
        event = rng.getrandbits(1)
        run(["/tmp/flush", target, "0"], ATTACKER)
        window_start = time.monotonic_ns()
        run(["/tmp/victim-static", "run", target, truth, str(trial), str(event)],
            VICTIM)
        remaining = window_start + WINDOW_NS - time.monotonic_ns()
        if remaining <= 0:
            raise RuntimeError("victim exceeded observation window")
        time.sleep(remaining / 1_000_000_000)
        timestamp = time.monotonic_ns()
        if primitive == "upstream":
            output = run(["/tmp/monitor-preadv2", target, "0"], ATTACKER,
                         capture=True).stdout
        else:
            output = run(["/tmp/monitor-modern-once", primitive, target],
                         ATTACKER, capture=True).stdout
        value, result = prediction(primitive, output)
        log.write(f"{trial},{timestamp},{value},{result}\n")
        log.flush()

    log.close()
    readable = subprocess.run(["sudo", "-u", ATTACKER, "test", "-r", truth]).returncode == 0
    setup = f"{prefix}-setup.txt"
    with open(setup, "w") as out:
        out.write(f"primitive={primitive}\n")
        out.write(f"kernel={run(['uname', '-r'], capture=True).stdout.strip()}\n")
        out.write(f"trials={count}\nseed={seed}\np_event=0.5\n")
        out.write(f"observation_window_ns={WINDOW_NS}\n")
        out.write(run(["id", ATTACKER], capture=True).stdout)
        out.write(run(["id", VICTIM], capture=True).stdout)
        out.write(f"attacker_ground_truth_readable={'yes' if readable else 'no'}\n")
        files = ["/tmp/victim-static", "/tmp/target.bin", "/tmp/flush"]
        files.append("/tmp/monitor-preadv2" if primitive == "upstream"
                     else "/tmp/monitor-modern-once")
        out.write(run(["sha256sum", *files], capture=True).stdout)

    if readable:
        sys.exit("ground truth separation failed")
    run(["install", "-m", "0644", attacker_log, f"{prefix}-attacker.csv"])
    run(["install", "-m", "0644", truth, f"{prefix}-ground-truth.csv"])
    print(f"{primitive}: {count} trials")


if __name__ == "__main__":
    main()
