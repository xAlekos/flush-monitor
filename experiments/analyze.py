#!/usr/bin/env python3
import csv
import sys
from pathlib import Path


PRIMITIVES = ("upstream", "flush", "dontcache")


def rows(path, value):
    with path.open(newline="") as handle:
        return {int(row["trial_id"]): int(row[value])
                for row in csv.DictReader(handle)}


def kernel(path):
    for line in path.read_text().splitlines():
        if line.startswith("kernel="):
            return line.split("=", 1)[1]


def ratio(num, den):
    return f"{num / den:.6f}"


def main():
    if len(sys.argv) != 3:
        sys.exit(f"usage: {sys.argv[0]} RAW_DIR OUTPUT_DIR")
    raw = Path(sys.argv[1])
    output = Path(sys.argv[2])
    output.mkdir(parents=True, exist_ok=True)
    metrics = []

    for primitive in PRIMITIVES:
        truth = rows(raw / f"m6-{primitive}-ground-truth.csv", "event")
        prediction = rows(raw / f"m6-{primitive}-attacker.csv", "prediction")

        tp = sum(truth[i] == 1 and prediction[i] == 1 for i in truth)
        tn = sum(truth[i] == 0 and prediction[i] == 0 for i in truth)
        fp = sum(truth[i] == 0 and prediction[i] == 1 for i in truth)
        fn = sum(truth[i] == 1 and prediction[i] == 0 for i in truth)
        n = len(truth)
        metrics.append({
            "primitive": primitive,
            "kernel": kernel(raw / f"m6-{primitive}-setup.txt"),
            "n": n,
            "tp": tp,
            "tn": tn,
            "fp": fp,
            "fn": fn,
            "accuracy": ratio(tp + tn, n),
            "precision": ratio(tp, tp + fp),
            "recall": ratio(tp, tp + fn),
            "f1": ratio(2 * tp, 2 * tp + fp + fn),
            "fpr": ratio(fp, fp + tn),
            "fnr": ratio(fn, fn + tp),
        })

    fields = list(metrics[0])
    with (output / "baseline_metrics.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(metrics)

    writer = csv.DictWriter(sys.stdout, fieldnames=fields, lineterminator="\n")
    writer.writeheader()
    writer.writerows(metrics)


if __name__ == "__main__":
    main()
