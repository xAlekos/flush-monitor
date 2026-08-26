#!/usr/bin/env python3
import csv
import math
from pathlib import Path
import sys


def ratio(a, b):
    return a / b


def wilson(successes, total):
    z = 1.96
    p = successes / total
    base = 1 + z * z / total
    centre = p + z * z / (2 * total)
    margin = z * math.sqrt(p * (1 - p) / total + z * z / (4 * total * total))
    return (centre - margin) / base, (centre + margin) / base


def p99(values):
    values = sorted(values)
    return values[math.ceil(len(values) * .99) - 1]


def read(path):
    with path.open(newline="") as file:
        return list(csv.DictReader(file))


def summarize(rows):
    tp = tn = fp = fn = 0
    errors = baseline_failures = cleanup_failures = timing_invalid = 0
    delays, cycles = [], []

    for row in rows:
        event, guess = int(row["event"]), int(row["prediction"])
        tp += event == 1 and guess == 1
        tn += event == 0 and guess == 0
        fp += event == 0 and guess == 1
        fn += event == 1 and guess == 0
        errors += any(int(row[name]) for name in
                      ("attacker_error", "victim_error", "validator_error"))
        baseline_failures += int(row["baseline_cached"]) != 0
        cleanup_failures += int(row["cleanup_cached"]) != 0

        flush_end = int(row["flush_end_ns"])
        baseline_start = int(row["baseline_check_start_ns"])
        baseline_end = int(row["baseline_check_end_ns"])
        event_start = int(row["event_start_ns"])
        monitor_start = int(row["monitor_start_ns"])
        timing_invalid += (flush_end > baseline_start or
                           baseline_end > event_start or
                           monitor_start < event_start)
        delays.append(monitor_start - event_start)
        cycles.append(int(row["cleanup_end_ns"]) - int(row["flush_start_ns"]))

    n = len(rows)
    accuracy = ratio(tp + tn, n)
    recall = ratio(tp, tp + fn)
    fpr = ratio(fp, fp + tn)
    cleanup_rate = ratio(cleanup_failures, n)
    timing_rate = ratio(timing_invalid, n)
    recall_low = wilson(tp, tp + fn)[0]
    fpr_high = wilson(fp, fp + tn)[1]
    cleanup_high = wilson(cleanup_failures, n)[1]
    timing_high = wilson(timing_invalid, n)[1]
    empirical = (accuracy >= .99 and recall >= .99 and fpr <= .01 and
                 not errors and not baseline_failures and
                 cleanup_rate <= .01 and timing_rate <= .01)
    confirmed = (empirical and n >= 10000 and recall_low >= .99 and
                 fpr_high <= .01 and cleanup_high <= .01 and timing_high <= .01)
    first = rows[0]
    return {
        "mode": first["mode"], "kernel": first["kernel"],
        "interval_us": int(first["interval_us"]), "n": n,
        "tp": tp, "tn": tn, "fp": fp, "fn": fn,
        "accuracy": accuracy, "precision": ratio(tp, tp + fp),
        "recall": recall, "recall_wilson_low95": recall_low,
        "f1": ratio(2 * tp, 2 * tp + fp + fn),
        "fpr": fpr, "fpr_wilson_high95": fpr_high,
        "errors": errors, "baseline_failures": baseline_failures,
        "cleanup_failures": cleanup_failures,
        "cleanup_failure_rate": cleanup_rate,
        "cleanup_wilson_high95": cleanup_high,
        "timing_invalid": timing_invalid, "timing_invalid_rate": timing_rate,
        "timing_invalid_wilson_high95": timing_high,
        "observed_delay_p99_ns": p99(delays),
        "full_cycle_p99_ns": p99(cycles),
        "empirical_candidate": int(empirical), "confirmed": int(confirmed),
    }


def save(path, data):
    fields = list(data[0])
    with path.open("w", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=fields)
        writer.writeheader()
        for row in data:
            writer.writerow({key: f"{value:.6f}" if isinstance(value, float)
                             else value for key, value in row.items()})


def plot(path, rows):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    for mode in ("flush", "dontcache"):
        selected = sorted((row for row in rows if row["mode"] == mode),
                          key=lambda row: row["interval_us"])
        plt.plot([row["interval_us"] for row in selected],
                 [row["f1"] for row in selected], marker="o", label=mode)
    plt.xscale("log")
    plt.ylim(0, 1.02)
    plt.xlabel("Observation window (µs)")
    plt.ylabel("F1")
    plt.grid(alpha=.25)
    plt.legend()
    plt.tight_layout()
    plt.savefig(path)
    plt.close()


def main():
    if len(sys.argv) != 4:
        sys.exit(f"usage: {sys.argv[0]} RAW_DIR OUTPUT_DIR PLOT_DIR")
    raw, output, plots = map(Path, sys.argv[1:])
    output.mkdir(parents=True, exist_ok=True)
    plots.mkdir(parents=True, exist_ok=True)
    summaries = [summarize(read(path)) for path in
                 sorted(raw.glob("window-sweep-*-*us.csv"))]
    summaries.sort(key=lambda row: (row["mode"], row["interval_us"]))
    save(output / "window-sweep-metrics.csv", summaries)

    best = []
    for mode in ("dontcache", "flush"):
        candidates = [row for row in summaries
                      if row["mode"] == mode and row["confirmed"]]
        selected = min(candidates, key=lambda row: row["interval_us"])
        lower = max((row for row in summaries if row["mode"] == mode and
                     row["interval_us"] < selected["interval_us"]),
                    key=lambda row: row["interval_us"])
        best.append({
            "mode": mode, "kernel": selected["kernel"], "status": "confirmed",
            "best_interval_us": selected["interval_us"], "n": selected["n"],
            "accuracy": selected["accuracy"], "f1": selected["f1"],
            "recall": selected["recall"],
            "recall_wilson_low95": selected["recall_wilson_low95"],
            "fpr": selected["fpr"],
            "fpr_wilson_high95": selected["fpr_wilson_high95"],
            "cleanup_failure_rate": selected["cleanup_failure_rate"],
            "cleanup_wilson_high95": selected["cleanup_wilson_high95"],
            "full_cycle_p99_ns": selected["full_cycle_p99_ns"],
            "lower_tested_interval_us": lower["interval_us"],
            "lower_n": lower["n"], "lower_recall": lower["recall"],
            "lower_confirmed": lower["confirmed"],
        })
    save(output / "window-sweep-minima.csv", best)
    plot(plots / "window-sweep-f1-vs-window.svg", summaries)
    for row in best:
        print(row["mode"], f"best_window={row['best_interval_us']} us")


if __name__ == "__main__":
    main()
