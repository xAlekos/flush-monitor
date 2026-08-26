#!/usr/bin/env python3
import csv
import math
from pathlib import Path
import statistics
import sys


def rows(path):
    with path.open(newline="") as file:
        return list(csv.DictReader(file))


def ratio(a, b):
    return a / b


def information(tp, tn, fp, fn):
    total = tp + tn + fp + fn
    actual = (tn + fp, tp + fn)
    predicted = (tn + fn, tp + fp)
    value = 0.0
    for event, guess, count in ((1, 1, tp), (0, 0, tn), (0, 1, fp), (1, 0, fn)):
        if count:
            value += count / total * math.log2(
                count * total / (actual[event] * predicted[guess])
            )
    return value


def percentile(values, q):
    values = sorted(values)
    position = (len(values) - 1) * q
    low, high = math.floor(position), math.ceil(position)
    return values[low] + (values[high] - values[low]) * (position - low)


def summary(values):
    return {
        "n": len(values),
        "mean_ns": statistics.fmean(values),
        "p50_ns": percentile(values, .50),
        "p95_ns": percentile(values, .95),
        "p99_ns": percentile(values, .99),
    }


def kernel(path):
    for line in path.read_text().splitlines():
        if line.startswith("kernel="):
            return line.split("=", 1)[1]


def save(path, data):
    with path.open("w", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=data[0])
        writer.writeheader()
        for row in data:
            writer.writerow({
                key: f"{value:.6f}" if isinstance(value, float) else value
                for key, value in row.items()
            })


def security(raw):
    output = []
    for policy in ("normal", "constant"):
        for primitive in ("flush", "dontcache"):
            name = f"mitigation-{policy}-{primitive}"
            events = {row["trial_id"]: int(row["event"])
                      for row in rows(raw / f"{name}-events.csv")}
            guesses = {row["trial_id"]: int(row["prediction"])
                       for row in rows(raw / f"{name}-predictions.csv")}
            pairs = [(event, guesses[trial]) for trial, event in events.items()]
            tp = sum(event and guess for event, guess in pairs)
            tn = sum(not event and not guess for event, guess in pairs)
            fp = sum(not event and guess for event, guess in pairs)
            fn = sum(event and not guess for event, guess in pairs)
            tpr = ratio(tp, tp + fn)
            fpr = ratio(fp, fp + tn)
            precision = ratio(tp, tp + fp)
            output.append({
                "policy": policy, "primitive": primitive,
                "kernel": kernel(raw / f"{name}-setup.txt"),
                "trials": len(pairs), "tp": tp, "tn": tn, "fp": fp, "fn": fn,
                "accuracy": ratio(tp + tn, len(pairs)),
                "precision": precision, "recall": tpr,
                "f1": ratio(2 * tp, 2 * tp + fp + fn),
                "tpr": tpr, "fpr": fpr,
                "leakage_advantage": abs(tpr - fpr),
                "mutual_information_bits": information(tp, tn, fp, fn),
            })
    return output


def performance(raw):
    data = rows(raw / "mitigation-victim-performance-raw.csv")
    output, grouped = [], {}
    for policy in ("normal", "constant"):
        selected = [row for row in data if row["policy"] == policy]
        for event in ("0", "1", "all"):
            values = [int(row["duration_ns"]) for row in selected
                      if event == "all" or row["event"] == event]
            grouped[policy, event] = summary(values)
            output.append({"policy": policy, "event": event,
                           **grouped[policy, event]})

    overhead = []
    for event in ("0", "1", "all"):
        before = grouped["normal", event]["mean_ns"]
        after = grouped["constant", event]["mean_ns"]
        overhead.append({
            "event": event, "normal_mean_ns": before,
            "constant_mean_ns": after, "mean_delta_ns": after - before,
            "overhead_percent": ratio(after - before, before) * 100,
        })
    return output, overhead


def main():
    raw, output = map(Path, sys.argv[1:3])
    output.mkdir(parents=True, exist_ok=True)
    metrics = security(raw)
    timings, overhead = performance(raw)
    save(output / "mitigation-security-metrics.csv", metrics)
    save(output / "mitigation-victim-performance.csv", timings)
    save(output / "mitigation-victim-overhead.csv", overhead)
    for row in metrics:
        print(row["policy"], row["primitive"],
              f"accuracy={row['accuracy']:.3f}",
              f"information={row['mutual_information_bits']:.3f} bit")


if __name__ == "__main__":
    main()
