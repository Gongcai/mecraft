#!/usr/bin/env python3
"""Run Intel VTune repeatedly and export one CSV report per run."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import time
from pathlib import Path


DEFAULT_VTUNE_PATH = Path(r"C:\Program Files (x86)\Intel\oneAPI\vtune\2026.2\bin64\vtune.exe")


def resolve_vtune(explicit: Path | None) -> Path:
    if explicit is not None:
        return explicit
    found = shutil.which("vtune")
    if found:
        return Path(found)
    return DEFAULT_VTUNE_PATH


def run_command(command: list[str], dry_run: bool) -> int:
    print(" ".join(f'"{part}"' if " " in part else part for part in command))
    if dry_run:
        return 0
    return subprocess.run(command, check=False).returncode


def main() -> int:
    parser = argparse.ArgumentParser(description="Run repeated VTune collections and export CSV reports.")
    parser.add_argument("--vtune", type=Path, help="Path to vtune.exe.")
    parser.add_argument("--analysis", default="memory-access", help="VTune analysis type, for example memory-access or uarch-exploration.")
    parser.add_argument("--report", default="hotspots", help="VTune report name to export.")
    parser.add_argument("--group-by", default="function", help="VTune report grouping.")
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--duration", type=float, required=True, help="Collection duration in seconds.")
    parser.add_argument("--resume-after", type=float, default=5.0, help="Delay collection after app start.")
    parser.add_argument("--result-root", type=Path, default=Path("vtune_runs"))
    parser.add_argument("--app-working-dir", type=Path, default=Path("."))
    parser.add_argument("--data-limit", type=int, default=0, help="VTune result size limit in MB. 0 means unlimited.")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("app", type=Path, help="Application executable.")
    parser.add_argument("app_args", nargs=argparse.REMAINDER, help="Arguments passed to the application after --.")
    args = parser.parse_args()

    vtune = resolve_vtune(args.vtune)
    if not vtune.exists():
        raise SystemExit(f"VTune executable not found: {vtune}")
    if args.runs <= 0:
        raise SystemExit("--runs must be positive.")
    if args.duration <= 0.0:
        raise SystemExit("--duration must be positive.")

    result_root = args.result_root.resolve()
    result_root.mkdir(parents=True, exist_ok=True)
    app_working_dir = args.app_working_dir.resolve()
    app = args.app.resolve()

    csv_paths: list[str] = []
    manifest = {
        "vtune": str(vtune),
        "analysis": args.analysis,
        "report": args.report,
        "group_by": args.group_by,
        "runs": args.runs,
        "duration": args.duration,
        "resume_after": args.resume_after,
        "app": str(app),
        "app_args": args.app_args,
        "results": [],
    }

    for run_index in range(1, args.runs + 1):
        stamp = time.strftime("%Y%m%d_%H%M%S")
        result_dir = result_root / f"{args.analysis}_run_{run_index:02d}_{stamp}"
        csv_path = result_root / f"{args.analysis}_run_{run_index:02d}_{stamp}.csv"

        collect_command = [
            str(vtune),
            "-collect", args.analysis,
            "-result-dir", str(result_dir),
            "-duration", f"{args.duration:g}",
            "-resume-after", f"{args.resume_after:g}",
            "-data-limit", str(args.data_limit),
            "-app-working-dir", str(app_working_dir),
            "--",
            str(app),
            *args.app_args,
        ]
        collect_code = run_command(collect_command, args.dry_run)
        if collect_code != 0:
            raise SystemExit(f"VTune collection failed for run {run_index}: exit code {collect_code}")

        report_command = [
            str(vtune),
            "-report", args.report,
            "-result-dir", str(result_dir),
            "-group-by", args.group_by,
            "-format", "csv",
            "-csv-delimiter", "comma",
            "-report-output", str(csv_path),
        ]
        report_code = run_command(report_command, args.dry_run)
        if report_code != 0:
            raise SystemExit(f"VTune report export failed for run {run_index}: exit code {report_code}")

        csv_paths.append(str(csv_path))
        manifest["results"].append({"result_dir": str(result_dir), "csv": str(csv_path)})  # type: ignore[index]

    manifest_path = result_root / "manifest.json"
    if not args.dry_run:
        manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"\nManifest: {manifest_path}")
    print("CSV files:")
    for csv_path in csv_paths:
        print(csv_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
