#!/usr/bin/env python3
"""Compare two Intel VTune CSV exports by function or synchronization object."""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path


COUNT_METRICS = {
    "Clockticks",
    "Instructions Retired",
    "Loads",
    "Stores",
    "LLC Miss Count",
    "Wait Count",
}

PRIMARY_METRICS = [
    "CPU Time",
    "Wait Time",
    "LLC Miss Count",
    "Loads",
    "Stores",
    "L1 Bound",
    "L2 Bound",
    "L3 Bound",
    "DRAM Bound",
    "Memory Bound",
    "DTLB Overhead",
    "Lock Latency",
    "Back-End Bound",
    "Front-End Bound",
    "Serializing Operations",
    "Store Bound",
    "Load Bound",
]

THREAD_OBJECT_ID_RE = re.compile(r"^(Thread|Mutex|Semaphore|Event|Condition Variable) 0x[0-9a-fA-F]+$")


@dataclass
class Entry:
    key: str
    label: str
    module: str = ""
    source: str = ""
    metrics: dict[str, float] = field(default_factory=lambda: defaultdict(float))
    metric_weights: dict[str, float] = field(default_factory=lambda: defaultdict(float))


def parse_float(value: str) -> float:
    if value == "":
        return 0.0
    try:
        return float(value)
    except ValueError:
        return 0.0


def normalize_header(name: str) -> str:
    name = name.strip()
    if ":" in name:
        name = name.split(":", 1)[0].strip()
    if "(" in name and name.endswith(")"):
        name = name[:name.rfind("(")].strip()
    return name


def header_indices(header: list[str]) -> dict[str, list[int]]:
    indices: dict[str, list[int]] = defaultdict(list)
    for index, name in enumerate(header):
        indices[name].append(index)
        normalized = normalize_header(name)
        if normalized != name:
            indices[normalized].append(index)
    return indices


def value_for_metric(row: list[str], indices: dict[str, list[int]], metric: str) -> float:
    values = [parse_float(row[index]) for index in indices.get(metric, []) if index < len(row)]
    if not values:
        return 0.0
    if metric in COUNT_METRICS or metric.endswith("Time"):
        return sum(values)
    return max(values)


def is_additive_metric(metric: str) -> bool:
    return metric in COUNT_METRICS or metric.endswith("Time")


def first_text(row: list[str], indices: dict[str, list[int]], names: list[str]) -> str:
    for name in names:
        for index in indices.get(name, []):
            if index < len(row) and row[index]:
                return row[index]
    return ""


def make_key(row: list[str], indices: dict[str, list[int]]) -> tuple[str, str, str, str]:
    module = first_text(row, indices, ["Module"])
    source = first_text(row, indices, ["Source File"])
    function = first_text(row, indices, ["Function (Full)", "Function / Call Stack", "Source Function / Function / Call Stack"])
    sync_object = first_text(row, indices, ["Sync Object / Function / Call Stack"])
    created = first_text(row, indices, ["Object Creation Module and Function"])
    object_type = first_text(row, indices, ["Object Type"])

    if sync_object:
        if function and function != "[Unknown]":
            label = function
        elif created and created != "[Unknown]":
            label = created
        else:
            label = sync_object

        comparable_object = "" if THREAD_OBJECT_ID_RE.match(sync_object) else sync_object
        key = f"sync|{object_type}|{created}|{function}|{comparable_object}"
        return key, label, module, source

    label = function or first_text(row, indices, ["Function / Call Stack", "Source Function / Function / Call Stack"])
    key = f"func|{module}|{source}|{label}"
    return key, label, module, source


def read_vtune_csv(path: Path) -> tuple[dict[str, Entry], list[str]]:
    with path.open("r", encoding="utf-8-sig", errors="replace", newline="") as handle:
        reader = csv.reader(handle)
        header = next(reader)
        indices = header_indices(header)
        metric_names = [name for name in PRIMARY_METRICS if name in indices]
        entries: dict[str, Entry] = {}

        for row in reader:
            if len(row) != len(header):
                continue
            key, label, module, source = make_key(row, indices)
            if not label:
                continue
            entry = entries.get(key)
            if entry is None:
                entry = Entry(key=key, label=label, module=module, source=source)
                entries[key] = entry
            row_cpu_time = value_for_metric(row, indices, "CPU Time")
            for metric in metric_names:
                value = value_for_metric(row, indices, metric)
                if is_additive_metric(metric):
                    entry.metrics[metric] += value
                elif row_cpu_time > 0.0:
                    entry.metrics[metric] += value * row_cpu_time
                    entry.metric_weights[metric] += row_cpu_time

    for entry in entries.values():
        for metric, weight in entry.metric_weights.items():
            if weight > 0.0:
                entry.metrics[metric] /= weight

    return entries, metric_names


def shorten(text: str, limit: int) -> str:
    text = " ".join(text.split())
    return text if len(text) <= limit else text[: limit - 3] + "..."


def percent_change(old: float, new: float) -> str:
    if old == 0.0:
        return "new" if new != 0.0 else "0.0%"
    return f"{((new - old) / abs(old)) * 100.0:+.1f}%"


def build_diff(old_entries: dict[str, Entry], new_entries: dict[str, Entry], metrics: list[str]) -> list[dict[str, object]]:
    keys = set(old_entries) | set(new_entries)
    rows: list[dict[str, object]] = []
    for key in keys:
        old = old_entries.get(key)
        new = new_entries.get(key)
        entry = new or old
        if entry is None:
            continue
        metric_values = {}
        for metric in metrics:
            old_value = old.metrics.get(metric, 0.0) if old else 0.0
            new_value = new.metrics.get(metric, 0.0) if new else 0.0
            metric_values[metric] = {
                "old": old_value,
                "new": new_value,
                "delta": new_value - old_value,
                "percent": percent_change(old_value, new_value),
            }
        rows.append({
            "key": key,
            "label": entry.label,
            "module": entry.module,
            "source": entry.source,
            "metrics": metric_values,
        })
    return rows


def print_summary(name: str, entries: dict[str, Entry], metrics: list[str]) -> None:
    print(f"\n{name}: {len(entries)} grouped rows")
    for metric in metrics:
        if not is_additive_metric(metric):
            continue
        total = sum(entry.metrics.get(metric, 0.0) for entry in entries.values())
        if total != 0.0:
            print(f"  {metric}: {total:.6g}")


def print_rank(title: str, rows: list[dict[str, object]], metric: str, top: int, reverse: bool) -> None:
    def delta(row: dict[str, object]) -> float:
        metrics = row["metrics"]
        return metrics[metric]["delta"]  # type: ignore[index]

    ranked = sorted(rows, key=delta, reverse=reverse)
    ranked = [row for row in ranked if delta(row) != 0.0][:top]
    if not ranked:
        return
    print(f"\n{title} ({metric})")
    print("rank\told\tnew\tdelta\tchange\tsource\tfunction")
    for index, row in enumerate(ranked, 1):
        metric_data = row["metrics"][metric]  # type: ignore[index]
        print(
            f"{index}\t"
            f"{metric_data['old']:.6g}\t"
            f"{metric_data['new']:.6g}\t"
            f"{metric_data['delta']:+.6g}\t"
            f"{metric_data['percent']}\t"
            f"{shorten(str(row['source']), 28)}\t"
            f"{shorten(str(row['label']), 120)}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare two Intel VTune CSV exports.")
    parser.add_argument("old_csv", type=Path)
    parser.add_argument("new_csv", type=Path)
    parser.add_argument("--metric", action="append", dest="metrics", help="Metric to compare. Can be repeated.")
    parser.add_argument("--top", type=int, default=15)
    parser.add_argument("--json", type=Path, dest="json_path", help="Write full diff as JSON.")
    args = parser.parse_args()

    old_entries, old_metrics = read_vtune_csv(args.old_csv)
    new_entries, new_metrics = read_vtune_csv(args.new_csv)
    available_metrics = [metric for metric in PRIMARY_METRICS if metric in set(old_metrics) | set(new_metrics)]
    selected_metrics = args.metrics or [metric for metric in available_metrics if metric in {"CPU Time", "Wait Time", "LLC Miss Count", "DRAM Bound", "L3 Bound", "L2 Bound", "L1 Bound", "Load Bound"}]
    selected_metrics = [metric for metric in selected_metrics if metric in available_metrics]

    print_summary("OLD", old_entries, selected_metrics)
    print_summary("NEW", new_entries, selected_metrics)

    diff_rows = build_diff(old_entries, new_entries, selected_metrics)
    for metric in selected_metrics:
        print_rank("Regressions", diff_rows, metric, args.top, True)
        print_rank("Improvements", diff_rows, metric, args.top, False)

    if args.json_path:
        payload = {
            "old": str(args.old_csv),
            "new": str(args.new_csv),
            "metrics": selected_metrics,
            "rows": diff_rows,
        }
        args.json_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
        print(f"\nJSON written: {args.json_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
