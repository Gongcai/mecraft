#!/usr/bin/env python3
"""Summarize repeated Intel VTune CSV exports with basic statistics."""

from __future__ import annotations

import argparse
import glob
import json
import math
import sys
from dataclasses import dataclass
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from compare_vtune_csv import PRIMARY_METRICS, Entry, read_vtune_csv, shorten  # noqa: E402


@dataclass
class MetricStats:
    n: int
    mean: float
    stdev: float
    ci95: float


def t_critical_95(df: int) -> float:
    if df <= 1:
        return 12.706
    if df == 2:
        return 4.303
    if df == 3:
        return 3.182
    if df == 4:
        return 2.776
    if df == 5:
        return 2.571
    if df == 6:
        return 2.447
    if df == 7:
        return 2.365
    if df == 8:
        return 2.306
    if df == 9:
        return 2.262
    if df <= 12:
        return 2.201
    if df <= 20:
        return 2.086
    if df <= 30:
        return 2.042
    if df <= 60:
        return 2.000
    return 1.960


def stats_for(values: list[float]) -> MetricStats:
    n = len(values)
    if n == 0:
        return MetricStats(n=0, mean=0.0, stdev=0.0, ci95=0.0)
    mean = sum(values) / n
    if n == 1:
        return MetricStats(n=n, mean=mean, stdev=0.0, ci95=0.0)
    variance = sum((value - mean) ** 2 for value in values) / (n - 1)
    stdev = math.sqrt(variance)
    ci95 = t_critical_95(n - 1) * stdev / math.sqrt(n)
    return MetricStats(n=n, mean=mean, stdev=stdev, ci95=ci95)


def percent_change(old: float, new: float) -> str:
    if old == 0.0:
        return "new" if new != 0.0 else "0.0%"
    return f"{((new - old) / abs(old)) * 100.0:+.1f}%"


def load_runs(paths: list[Path]) -> tuple[list[dict[str, Entry]], list[str]]:
    runs: list[dict[str, Entry]] = []
    available: set[str] = set()
    for path in paths:
        entries, metrics = read_vtune_csv(path)
        runs.append(entries)
        available.update(metrics)
    return runs, [metric for metric in PRIMARY_METRICS if metric in available]


def expand_paths(paths: list[Path]) -> list[Path]:
    expanded: list[Path] = []
    for path in paths:
        text = str(path)
        if any(char in text for char in "*?[]"):
            matches = [Path(match) for match in sorted(glob.glob(text))]
            if not matches:
                raise SystemExit(f"No files matched: {text}")
            expanded.extend(matches)
        else:
            expanded.append(path)
    return expanded


def collect_values(runs: list[dict[str, Entry]], key: str, metric: str) -> list[float]:
    return [run.get(key, Entry(key=key, label="")).metrics.get(metric, 0.0) for run in runs]


def label_for(key: str, old_runs: list[dict[str, Entry]], new_runs: list[dict[str, Entry]]) -> tuple[str, str]:
    for run in new_runs + old_runs:
        entry = run.get(key)
        if entry is not None:
            return entry.label, entry.source
    return key, ""


def print_single_group(runs: list[dict[str, Entry]], metrics: list[str], top: int) -> list[dict[str, object]]:
    keys = set().union(*(run.keys() for run in runs)) if runs else set()
    rows: list[dict[str, object]] = []
    for key in keys:
        label, source = label_for(key, [], runs)
        row = {"key": key, "label": label, "source": source, "metrics": {}}
        for metric in metrics:
            row["metrics"][metric] = stats_for(collect_values(runs, key, metric))  # type: ignore[index]
        rows.append(row)

    for metric in metrics:
        ranked = sorted(rows, key=lambda row: row["metrics"][metric].mean, reverse=True)[:top]  # type: ignore[index]
        print(f"\nTop Mean ({metric})")
        print("rank\tn\tmean\tstdev\tci95\trsd\tsource\tfunction")
        for index, row in enumerate(ranked, 1):
            stat: MetricStats = row["metrics"][metric]  # type: ignore[index]
            rsd = (stat.stdev / stat.mean * 100.0) if stat.mean != 0.0 else 0.0
            print(
                f"{index}\t{stat.n}\t{stat.mean:.6g}\t{stat.stdev:.6g}\t{stat.ci95:.6g}\t"
                f"{rsd:.1f}%\t{shorten(str(row['source']), 28)}\t{shorten(str(row['label']), 120)}"
            )
    return rows


def print_group_diff(old_runs: list[dict[str, Entry]],
                     new_runs: list[dict[str, Entry]],
                     metrics: list[str],
                     top: int) -> list[dict[str, object]]:
    keys = set().union(*(run.keys() for run in old_runs + new_runs)) if (old_runs or new_runs) else set()
    rows: list[dict[str, object]] = []
    for key in keys:
        label, source = label_for(key, old_runs, new_runs)
        metric_rows = {}
        for metric in metrics:
            old_stat = stats_for(collect_values(old_runs, key, metric))
            new_stat = stats_for(collect_values(new_runs, key, metric))
            metric_rows[metric] = {
                "old": old_stat,
                "new": new_stat,
                "delta": new_stat.mean - old_stat.mean,
                "percent": percent_change(old_stat.mean, new_stat.mean),
            }
        rows.append({"key": key, "label": label, "source": source, "metrics": metric_rows})

    for metric in metrics:
        for title, reverse in (("Regressions", True), ("Improvements", False)):
            ranked = sorted(rows, key=lambda row: row["metrics"][metric]["delta"], reverse=reverse)  # type: ignore[index]
            ranked = [row for row in ranked if row["metrics"][metric]["delta"] != 0.0][:top]  # type: ignore[index]
            if not ranked:
                continue
            print(f"\n{title} Mean ({metric})")
            print("rank\told_mean\tnew_mean\tdelta\tchange\told_ci95\tnew_ci95\tsource\tfunction")
            for index, row in enumerate(ranked, 1):
                data = row["metrics"][metric]  # type: ignore[index]
                old_stat: MetricStats = data["old"]
                new_stat: MetricStats = data["new"]
                print(
                    f"{index}\t{old_stat.mean:.6g}\t{new_stat.mean:.6g}\t{data['delta']:+.6g}\t"
                    f"{data['percent']}\t{old_stat.ci95:.6g}\t{new_stat.ci95:.6g}\t"
                    f"{shorten(str(row['source']), 28)}\t{shorten(str(row['label']), 120)}"
                )
    return rows


def serialize(obj: object) -> object:
    if isinstance(obj, MetricStats):
        return obj.__dict__
    raise TypeError(f"Object of type {type(obj).__name__} is not JSON serializable")


def main() -> int:
    parser = argparse.ArgumentParser(description="Summarize repeated VTune CSV exports.")
    parser.add_argument("csv", nargs="*", type=Path, help="CSV files for a single-group summary.")
    parser.add_argument("--old", action="append", type=Path, default=[], help="Old baseline CSV. Can be repeated.")
    parser.add_argument("--new", action="append", type=Path, default=[], help="New run CSV. Can be repeated.")
    parser.add_argument("--metric", action="append", dest="metrics", help="Metric to summarize. Can be repeated.")
    parser.add_argument("--top", type=int, default=15)
    parser.add_argument("--json", type=Path, dest="json_path", help="Write full statistics as JSON.")
    args = parser.parse_args()

    if (args.old or args.new) and args.csv:
        parser.error("Use either positional CSV files or --old/--new groups, not both.")
    if bool(args.old) != bool(args.new):
        parser.error("--old and --new must be provided together.")

    if args.old and args.new:
        old_paths = expand_paths(args.old)
        new_paths = expand_paths(args.new)
        old_runs, old_metrics = load_runs(old_paths)
        new_runs, new_metrics = load_runs(new_paths)
        available_metrics = [metric for metric in PRIMARY_METRICS if metric in set(old_metrics) | set(new_metrics)]
        selected_metrics = args.metrics or [metric for metric in available_metrics if metric in {"CPU Time", "LLC Miss Count", "DRAM Bound"}]
        selected_metrics = [metric for metric in selected_metrics if metric in available_metrics]
        print(f"OLD runs: {len(old_runs)}")
        print(f"NEW runs: {len(new_runs)}")
        rows = print_group_diff(old_runs, new_runs, selected_metrics, args.top)
    else:
        if not args.csv:
            parser.error("Provide CSV files or --old/--new groups.")
        csv_paths = expand_paths(args.csv)
        runs, available_metrics = load_runs(csv_paths)
        selected_metrics = args.metrics or [metric for metric in available_metrics if metric in {"CPU Time", "LLC Miss Count", "DRAM Bound"}]
        selected_metrics = [metric for metric in selected_metrics if metric in available_metrics]
        print(f"Runs: {len(runs)}")
        rows = print_single_group(runs, selected_metrics, args.top)

    if args.json_path:
        args.json_path.write_text(json.dumps(rows, indent=2, default=serialize), encoding="utf-8")
        print(f"\nJSON written: {args.json_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
