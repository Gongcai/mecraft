#!/usr/bin/env python3
"""Validate Mecraft projectile definition JSON files."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


DEFAULT_PROJECTILES_PATH = Path("assets/config/projectiles.json")
DEFAULT_ITEMS_PATH = Path("assets/config/items.json")
DEFAULT_BLOCKS_PATH = Path("assets/config/blocks.json")
DEFAULT_SOUNDS_PATH = Path("assets/sounds/sounds.json")


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError(f"{path} must contain a JSON object")
    return data


def collect_known_item_ids(items_path: Path, blocks_path: Path) -> set[str]:
    ids = {"minecraft:air"}
    if items_path.exists():
        items = load_json(items_path).get("items", [])
        if isinstance(items, list):
            for item in items:
                if isinstance(item, dict) and isinstance(item.get("id"), str):
                    ids.add(item["id"])

    if blocks_path.exists():
        blocks = load_json(blocks_path).get("blocks", [])
        if isinstance(blocks, list):
            for block in blocks:
                if isinstance(block, dict) and isinstance(block.get("id"), str):
                    ids.add(block["id"])
    return ids


def collect_known_block_ids(blocks_path: Path) -> set[str]:
    ids = {"minecraft:air"}
    if blocks_path.exists():
        blocks = load_json(blocks_path).get("blocks", [])
        if isinstance(blocks, list):
            for block in blocks:
                if isinstance(block, dict) and isinstance(block.get("id"), str):
                    ids.add(block["id"])
    return ids


def collect_known_sound_ids(sounds_path: Path) -> set[str]:
    if not sounds_path.exists():
        return set()
    sounds = load_json(sounds_path).get("sounds", {})
    if not isinstance(sounds, dict):
        return set()
    return {sound_id for sound_id in sounds.keys() if isinstance(sound_id, str)}


def require(condition: bool, message: str, errors: list[str]) -> None:
    if not condition:
        errors.append(message)


def validate_projectile(
    projectile: Any,
    index: int,
    known_items: set[str],
    known_blocks: set[str],
    known_sounds: set[str],
    seen_items: set[str],
    errors: list[str],
) -> None:
    context = f"projectiles[{index}]"
    if not isinstance(projectile, dict):
        errors.append(f"{context} must be an object")
        return

    item = projectile.get("item")
    require(isinstance(item, str) and item in known_items,
            f"{context}.item references an unknown item: {item}", errors)
    if isinstance(item, str):
        require(item not in seen_items, f"{context}.item is duplicated: {item}", errors)
        seen_items.add(item)

    for key in ("damage",):
        value = projectile.get(key)
        require(isinstance(value, int) and value > 0, f"{context}.{key} must be a positive integer", errors)

    for key in ("hitRadius", "throwSpeed", "boundsHalfExtent", "lifetimeSeconds"):
        value = projectile.get(key)
        require(isinstance(value, (int, float)) and value > 0,
                f"{context}.{key} must be a positive number", errors)

    for key in ("gravity", "spawnForwardOffset"):
        value = projectile.get(key)
        require(isinstance(value, (int, float)) and value >= 0,
                f"{context}.{key} must be a non-negative number", errors)

    upward_bias = projectile.get("upwardBias")
    require(isinstance(upward_bias, (int, float)), f"{context}.upwardBias must be a number", errors)
    spin = projectile.get("spinSpeedRadians")
    require(isinstance(spin, (int, float)), f"{context}.spinSpeedRadians must be a number", errors)

    throw_sound = projectile.get("throwSound", "")
    require(isinstance(throw_sound, str), f"{context}.throwSound must be a string", errors)
    if isinstance(throw_sound, str) and throw_sound:
        require(throw_sound in known_sounds,
                f"{context}.throwSound references an unknown sound: {throw_sound}", errors)

    impact = projectile.get("impact")
    require(isinstance(impact, dict), f"{context}.impact must be an object", errors)
    if not isinstance(impact, dict):
        return

    particle_block = impact.get("particleBlock", "")
    require(isinstance(particle_block, str), f"{context}.impact.particleBlock must be a string", errors)
    if isinstance(particle_block, str) and particle_block:
        require(particle_block in known_blocks,
                f"{context}.impact.particleBlock references an unknown block: {particle_block}", errors)

    particle_count = impact.get("particleCount", 0)
    require(isinstance(particle_count, int) and particle_count > 0,
            f"{context}.impact.particleCount must be a positive integer", errors)

    impact_sound = impact.get("sound", "")
    require(isinstance(impact_sound, str), f"{context}.impact.sound must be a string", errors)
    if isinstance(impact_sound, str) and impact_sound:
        require(impact_sound in known_sounds,
                f"{context}.impact.sound references an unknown sound: {impact_sound}", errors)


def validate_file(path: Path, items_path: Path, blocks_path: Path, sounds_path: Path) -> list[str]:
    data = load_json(path)
    projectiles = data.get("projectiles")
    errors: list[str] = []
    require(isinstance(projectiles, list), "root.projectiles must be an array", errors)
    if not isinstance(projectiles, list):
        return errors

    known_items = collect_known_item_ids(items_path, blocks_path)
    known_blocks = collect_known_block_ids(blocks_path)
    known_sounds = collect_known_sound_ids(sounds_path)
    seen_items: set[str] = set()
    for index, projectile in enumerate(projectiles):
        validate_projectile(projectile, index, known_items, known_blocks, known_sounds, seen_items, errors)
    return errors


def validate_command(args: argparse.Namespace) -> int:
    errors = validate_file(args.path, args.items, args.blocks, args.sounds)
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print(f"{args.path} is valid")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", nargs="?", type=Path, default=DEFAULT_PROJECTILES_PATH)
    parser.add_argument("--items", type=Path, default=DEFAULT_ITEMS_PATH)
    parser.add_argument("--blocks", type=Path, default=DEFAULT_BLOCKS_PATH)
    parser.add_argument("--sounds", type=Path, default=DEFAULT_SOUNDS_PATH)
    parser.set_defaults(func=validate_command)
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        return args.func(args)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
