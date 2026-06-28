#!/usr/bin/env python3
"""Summarize NVIDIA Nsight Graphics CSV exports for Mecraft frame captures."""

from __future__ import annotations

import argparse
import csv
import re
from dataclasses import dataclass
from pathlib import Path


TOP_LEVEL_NAMES = {
    "Shadow",
    "GBuffer",
    "Velocity",
    "SSAO",
    "DeferredLighting",
    "Reflection",
    "SceneComposite",
    "Cloud",
    "WaterComposite.PreTAA",
    "Volumetric",
    "TemporalResolve",
    "SkyCapture",
    "Transparent.Generic",
    "HeldItem.Block",
}

PHASE_NAMES = {"Opaque", "Transparent"}

NUMBER_RE = re.compile(r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)")
CASCADE_RE = re.compile(r"^Shadow\.CSM\.Cascade(\d+)$")
MDI_LABEL_RE = re.compile(r"^(?P<name>.+\.MDI) commands=(?P<commands>\d+) vertices=(?P<vertices>\d+)$")
MDI_API_RE = re.compile(r"^void glMultiDrawArraysIndirect\(")
MDI_API_DRAWS_RE = re.compile(r"drawcount = (?P<drawcount>\d+)")


@dataclass
class Row:
    event_id: int
    description: str
    cpu_ms: float | None
    gpu_ms: float | None
    issue: str
    context: str
    is_mdi_api: bool
    mdi_api_drawcount: int | None


@dataclass
class MdiBatch:
    event_id: int
    description: str
    gpu_ms: float | None
    cpu_ms: float | None
    commands: int
    vertices: int
    top: str | None
    cascade: int | None
    phase: str | None
    context: str


def parse_ms(value: str) -> float | None:
    value = (value or "").strip()
    if not value or value == "-":
        return None
    match = NUMBER_RE.search(value)
    if match is None:
        return None
    return float(match.group(0))


def parse_event_id(value: str) -> int:
    value = (value or "").strip()
    return int(value) if value.isdigit() else -1


def sanitize_issue(value: str) -> str:
    return " ".join((value or "").replace("\n", " ").split())


def read_rows(path: Path) -> tuple[list[Row], list[MdiBatch], str | None]:
    rows: list[Row] = []
    mdi_batches: list[MdiBatch] = []

    current_top: str | None = None
    current_cascade: int | None = None
    current_phase: str | None = None
    frame_name: str | None = None

    with path.open("r", encoding="utf-8-sig", errors="replace", newline="") as handle:
        reader = csv.DictReader(handle)
        fieldnames = reader.fieldnames or []
        issue_column = next((name for name in fieldnames if name.startswith("Issues")), None)
        for raw in reader:
            description = (raw.get("Description") or "").strip()
            event_id = parse_event_id(raw.get("Event", ""))
            cpu_ms = parse_ms(raw.get("CPU ms", ""))
            gpu_ms = parse_ms(raw.get("GPU ms", ""))
            issue = sanitize_issue(raw.get(issue_column, "") if issue_column is not None else "")

            cascade_match = CASCADE_RE.match(description)
            if description.startswith("Frame "):
                frame_name = description
                current_top = None
                current_cascade = None
                current_phase = None
            elif description in TOP_LEVEL_NAMES:
                current_top = description
                current_cascade = None
                current_phase = None
            elif cascade_match is not None:
                current_top = "Shadow"
                current_cascade = int(cascade_match.group(1))
                current_phase = None
            elif description in PHASE_NAMES:
                current_phase = description

            context_parts: list[str] = []
            if current_top is not None:
                context_parts.append(current_top)
            if current_cascade is not None:
                context_parts.append(f"Cascade{current_cascade}")
            if current_phase is not None:
                context_parts.append(current_phase)
            context = " / ".join(context_parts)

            mdi_api = MDI_API_RE.match(description) is not None
            drawcount_match = MDI_API_DRAWS_RE.search(description) if mdi_api else None
            mdi_api_drawcount = int(drawcount_match.group("drawcount")) if drawcount_match else None

            row = Row(
                event_id=event_id,
                description=description,
                cpu_ms=cpu_ms,
                gpu_ms=gpu_ms,
                issue=issue,
                context=context,
                is_mdi_api=mdi_api,
                mdi_api_drawcount=mdi_api_drawcount,
            )
            rows.append(row)

            mdi_label_match = MDI_LABEL_RE.match(description)
            if mdi_label_match is not None:
                mdi_batches.append(MdiBatch(
                    event_id=event_id,
                    description=description,
                    gpu_ms=gpu_ms,
                    cpu_ms=cpu_ms,
                    commands=int(mdi_label_match.group("commands")),
                    vertices=int(mdi_label_match.group("vertices")),
                    top=current_top,
                    cascade=current_cascade,
                    phase=current_phase,
                    context=context,
                ))

    return rows, mdi_batches, frame_name


def fmt_ms(value: float | None) -> str:
    return "-" if value is None else f"{value:.3f}"


def fmt_pct(value: float | None, total: float | None) -> str:
    if value is None or total is None or total <= 0.0:
        return "-"
    return f"{value / total * 100.0:.1f}%"


def print_rows(title: str, rows: list[Row], total_ms: float | None, limit: int) -> None:
    print()
    print(title)
    print("rank\tevent_id\tgpu_ms\tpct\tcpu_ms\tcontext\tevent")
    for rank, row in enumerate(rows[:limit], start=1):
        print(f"{rank}\t{row.event_id}\t{fmt_ms(row.gpu_ms)}\t{fmt_pct(row.gpu_ms, total_ms)}\t"
              f"{fmt_ms(row.cpu_ms)}\t{row.context}\t{row.description}")


def print_mdi_batches(batches: list[MdiBatch], total_ms: float | None, limit: int) -> None:
    batches = sorted(
        [batch for batch in batches if batch.gpu_ms is not None],
        key=lambda item: item.gpu_ms if item.gpu_ms is not None else -1.0,
        reverse=True,
    )
    print()
    print("Top MDI Batches")
    print("rank\tevent_id\tgpu_ms\tpct\tcommands\tvertices\tcontext\tevent")
    for rank, batch in enumerate(batches[:limit], start=1):
        print(f"{rank}\t{batch.event_id}\t{fmt_ms(batch.gpu_ms)}\t{fmt_pct(batch.gpu_ms, total_ms)}\t"
              f"{batch.commands}\t{batch.vertices}\t{batch.context}\t{batch.description}")


def print_shadow_cascades(rows: list[Row], batches: list[MdiBatch], total_ms: float | None) -> None:
    cascade_rows: dict[int, dict[str, object]] = {
        index: {
            "total": None,
            "opaque": None,
            "transparent": None,
            "opaque_mdi": [],
            "cutout_mdi": [],
            "transparent_mdi": [],
        }
        for index in range(4)
    }

    active_cascade: int | None = None
    for row in rows:
        cascade_match = CASCADE_RE.match(row.description)
        if cascade_match is not None:
            active_cascade = int(cascade_match.group(1))
            cascade_rows.setdefault(active_cascade, {})["total"] = row.gpu_ms
            continue
        if row.description in PHASE_NAMES and active_cascade is not None:
            cascade_rows.setdefault(active_cascade, {})[row.description.lower()] = row.gpu_ms

    for batch in batches:
        if batch.cascade is None:
            continue
        bucket = cascade_rows.setdefault(batch.cascade, {})
        if batch.description.startswith("Terrain.Opaque.MDI"):
            bucket.setdefault("opaque_mdi", []).append(batch)
        elif batch.description.startswith("Terrain.Cutout.MDI"):
            bucket.setdefault("cutout_mdi", []).append(batch)
        elif batch.description.startswith("Terrain.Transparent.MDI"):
            bucket.setdefault("transparent_mdi", []).append(batch)

    print()
    print("Shadow Cascades")
    print("cascade\ttotal_ms\tpct\topaque_ms\ttransparent_ms\topaque_mdi_ms\topaque_cmds\topaque_vertices\t"
          "cutout_mdi_ms\tcutout_cmds\tcutout_vertices\ttransparent_mdi_ms\ttransparent_cmds\ttransparent_vertices")
    for cascade in sorted(cascade_rows):
        bucket = cascade_rows[cascade]

        def sum_batches(key: str) -> tuple[float | None, int, int]:
            items = bucket.get(key, [])
            if not isinstance(items, list) or not items:
                return None, 0, 0
            ms = sum((item.gpu_ms or 0.0) for item in items)
            commands = sum(item.commands for item in items)
            vertices = sum(item.vertices for item in items)
            return ms, commands, vertices

        opaque_mdi_ms, opaque_cmds, opaque_vertices = sum_batches("opaque_mdi")
        cutout_mdi_ms, cutout_cmds, cutout_vertices = sum_batches("cutout_mdi")
        transparent_mdi_ms, transparent_cmds, transparent_vertices = sum_batches("transparent_mdi")
        total = bucket.get("total")
        opaque = bucket.get("opaque")
        transparent = bucket.get("transparent")
        print(f"{cascade}\t{fmt_ms(total if isinstance(total, float) else None)}\t"
              f"{fmt_pct(total if isinstance(total, float) else None, total_ms)}\t"
              f"{fmt_ms(opaque if isinstance(opaque, float) else None)}\t"
              f"{fmt_ms(transparent if isinstance(transparent, float) else None)}\t"
              f"{fmt_ms(opaque_mdi_ms)}\t{opaque_cmds}\t{opaque_vertices}\t"
              f"{fmt_ms(cutout_mdi_ms)}\t{cutout_cmds}\t{cutout_vertices}\t"
              f"{fmt_ms(transparent_mdi_ms)}\t{transparent_cmds}\t{transparent_vertices}")


def print_issues(rows: list[Row], limit: int) -> None:
    issue_rows = [row for row in rows if row.issue]
    issue_rows.sort(key=lambda item: item.gpu_ms if item.gpu_ms is not None else -1.0, reverse=True)
    print()
    print(f"Issues ({len(issue_rows)} rows with issue text)")
    print("rank\tevent_id\tgpu_ms\tcontext\tevent\tissue")
    for rank, row in enumerate(issue_rows[:limit], start=1):
        print(f"{rank}\t{row.event_id}\t{fmt_ms(row.gpu_ms)}\t{row.context}\t{row.description}\t{row.issue}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path, help="Nsight Graphics CSV export.")
    parser.add_argument("--top", type=int, default=25, help="Rows per table.")
    parser.add_argument("--include-mdi-api", action="store_true",
                        help="Include expanded glMultiDrawArraysIndirect API rows.")
    args = parser.parse_args()

    rows, mdi_batches, frame_name = read_rows(args.path)
    frame_row = next((row for row in rows if row.description.startswith("Frame ")), None)
    total_ms = frame_row.gpu_ms if frame_row is not None else None

    print(f"File: {args.path}")
    if frame_name is not None:
        print(f"Frame: {frame_name}")
    print(f"Rows parsed: {len(rows)}")
    print(f"Frame GPU Duration: {fmt_ms(total_ms)} ms")

    visible_rows = [row for row in rows if row.gpu_ms is not None]
    if not args.include_mdi_api:
        visible_rows = [row for row in visible_rows if not row.is_mdi_api]
    visible_rows.sort(key=lambda item: item.gpu_ms if item.gpu_ms is not None else -1.0, reverse=True)
    print_rows("Top Events", visible_rows, total_ms, args.top)
    print_shadow_cascades(rows, mdi_batches, total_ms)
    print_mdi_batches(mdi_batches, total_ms, args.top)
    print_issues(rows, args.top)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
