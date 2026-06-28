#!/usr/bin/env python3
"""Summarize RenderDoc Event Browser GPU duration text exports."""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable


MDI_SUBDRAW_RE = re.compile(r"^glMultiDrawArraysIndirect\[\d+\]\(")
MDI_BATCH_RE = re.compile(r"^glMultiDrawArraysIndirect\(<(\d+)>\)")


@dataclass
class EventNode:
    eid: int
    name: str
    action: str
    duration_us: float
    depth: int
    parent: "EventNode | None" = None
    children: list["EventNode"] = field(default_factory=list)

    @property
    def duration_ms(self) -> float:
        return self.duration_us / 1000.0

    @property
    def has_children(self) -> bool:
        return bool(self.children)


def parse_event_line(line: str) -> tuple[int, int, str, str, float] | None:
    parts = line.split("|")
    if len(parts) != 4:
        return None

    eid_text = parts[0].strip()
    event_text = parts[1].rstrip()
    action_text = parts[2].strip()
    duration_text = parts[3].strip()
    if not eid_text.isdigit():
        return None

    dash_index = event_text.find("-")
    if dash_index < 0:
        raise ValueError(f"Event row has no tree marker: {line!r}")

    name = event_text[dash_index + 1 :].strip()
    return int(eid_text), dash_index, name, action_text, float(duration_text)


def parse_renderdoc_duration_export(path: Path) -> list[EventNode]:
    nodes: list[EventNode] = []
    stack: list[EventNode] = []

    for line_number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), start=1):
        parsed = parse_event_line(line)
        if parsed is None:
            continue

        eid, depth, name, action, duration_us = parsed
        node = EventNode(eid=eid, name=name, action=action, duration_us=duration_us, depth=depth)

        while stack and stack[-1].depth >= depth:
            stack.pop()
        if stack:
            node.parent = stack[-1]
            stack[-1].children.append(node)

        nodes.append(node)
        stack.append(node)

    if not nodes:
        raise ValueError(f"No RenderDoc event rows found in {path}")
    return nodes


def iter_ancestors(node: EventNode) -> Iterable[EventNode]:
    parent = node.parent
    while parent is not None:
        yield parent
        parent = parent.parent


def path_for(node: EventNode, max_parts: int = 4) -> str:
    parts = [ancestor.name for ancestor in reversed(list(iter_ancestors(node)))]
    parts.append(node.name)
    if len(parts) > max_parts:
        parts = ["..."] + parts[-(max_parts - 1) :]
    return " / ".join(parts)


def is_mdi_subdraw(node: EventNode) -> bool:
    return MDI_SUBDRAW_RE.match(node.name) is not None


def is_action_range(action: str) -> bool:
    return "-" in action


def print_table(title: str, rows: list[EventNode], total_us: float, limit: int, include_path: bool = True) -> None:
    print()
    print(title)
    print("rank\teid\tms\tpct\taction\tevent")
    for rank, node in enumerate(rows[:limit], start=1):
        pct = (node.duration_us / total_us * 100.0) if total_us > 0.0 else 0.0
        event_name = path_for(node) if include_path else node.name
        print(f"{rank}\t{node.eid}\t{node.duration_ms:.3f}\t{pct:.1f}%\t{node.action}\t{event_name}")


def summarize_mdi_batches(nodes: list[EventNode], total_us: float, limit: int) -> None:
    batches = [node for node in nodes if MDI_BATCH_RE.match(node.name)]
    batches.sort(key=lambda node: node.duration_us, reverse=True)

    print()
    print("Top MDI Batches")
    print("rank\teid\tms\tpct\tcommands\tparent")
    for rank, node in enumerate(batches[:limit], start=1):
        match = MDI_BATCH_RE.match(node.name)
        commands = match.group(1) if match else ""
        pct = (node.duration_us / total_us * 100.0) if total_us > 0.0 else 0.0
        parent_name = node.parent.name if node.parent else ""
        print(f"{rank}\t{node.eid}\t{node.duration_ms:.3f}\t{pct:.1f}%\t{commands}\t{parent_name}")


def print_tree(node: EventNode, total_us: float, depth: int, max_depth: int, min_ms: float) -> None:
    if depth > max_depth or node.duration_ms < min_ms:
        return
    pct = (node.duration_us / total_us * 100.0) if total_us > 0.0 else 0.0
    indent = "  " * depth
    print(f"{indent}{node.duration_ms:8.3f} ms  {pct:5.1f}%  EID {node.eid:<5} {node.name}")
    for child in sorted(node.children, key=lambda item: item.duration_us, reverse=True):
        print_tree(child, total_us, depth + 1, max_depth, min_ms)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path, help="RenderDoc Event Browser text export with GPU durations.")
    parser.add_argument("--top", type=int, default=25, help="Number of rows to show per table.")
    parser.add_argument("--tree-depth", type=int, default=3, help="Maximum hierarchy depth for the tree view.")
    parser.add_argument("--tree-min-ms", type=float, default=0.05, help="Minimum event duration in tree view.")
    parser.add_argument("--include-mdi-subdraws", action="store_true",
                        help="Include glMultiDrawArraysIndirect[index] rows in leaf tables.")
    args = parser.parse_args()

    nodes = parse_renderdoc_duration_export(args.path)
    root = nodes[0]
    total_us = root.duration_us

    print(f"File: {args.path}")
    print(f"Root: EID {root.eid} {root.name}")
    print(f"Frame GPU Duration: {root.duration_ms:.3f} ms")
    print(f"Rows parsed: {len(nodes)}")

    top_level = sorted(root.children, key=lambda node: node.duration_us, reverse=True)
    print_table("Top-Level Events", top_level, total_us, args.top, include_path=False)

    grouped = [
        node for node in nodes[1:]
        if node.has_children or is_action_range(node.action)
    ]
    grouped.sort(key=lambda node: node.duration_us, reverse=True)
    print_table("Top Grouped Events", grouped, total_us, args.top)

    leaves = [node for node in nodes[1:] if not node.children]
    if not args.include_mdi_subdraws:
        leaves = [node for node in leaves if not is_mdi_subdraw(node)]
    leaves.sort(key=lambda node: node.duration_us, reverse=True)
    print_table("Top Leaf/API Events", leaves, total_us, args.top)

    summarize_mdi_batches(nodes, total_us, args.top)

    print()
    print(f"Tree View (sorted by duration, max_depth={args.tree_depth}, min_ms={args.tree_min_ms:.3f})")
    print_tree(root, total_us, 0, args.tree_depth, args.tree_min_ms)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
