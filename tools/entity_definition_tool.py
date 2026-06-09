#!/usr/bin/env python3
"""Validate and scaffold Mecraft entity definition JSON files."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


DEFAULT_ENTITIES_PATH = Path("assets/config/entities.json")
DEFAULT_ITEMS_PATH = Path("assets/config/items.json")
DEFAULT_BLOCKS_PATH = Path("assets/config/blocks.json")
DEFAULT_SOUNDS_PATH = Path("assets/sounds/sounds.json")
DEFAULT_MOB_TEXTURES_DIR = Path("assets/textures/entity/mobs")
SUPPORTED_MODELS = {"humanoid"}
TEXTURE_KEY_CHARS = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")


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


def validate_vec3(value: Any, context: str, errors: list[str]) -> None:
    require(isinstance(value, list) and len(value) == 3, f"{context} must be a 3-number array", errors)
    if isinstance(value, list):
        for index, component in enumerate(value):
            require(isinstance(component, (int, float)), f"{context}[{index}] must be a number", errors)


def validate_entity(
    entity: Any,
    index: int,
    known_items: set[str],
    known_blocks: set[str],
    known_sounds: set[str],
    mob_textures_dir: Path,
    seen_ids: set[str],
    errors: list[str],
) -> None:
    context = f"entities[{index}]"
    if not isinstance(entity, dict):
        errors.append(f"{context} must be an object")
        return

    entity_id = entity.get("id")
    require(isinstance(entity_id, str) and ":" in entity_id, f"{context}.id must be a namespaced id", errors)
    if isinstance(entity_id, str):
        require(entity_id not in seen_ids, f"{context}.id is duplicated: {entity_id}", errors)
        seen_ids.add(entity_id)

    kind = entity.get("kind", "mob")
    require(kind == "mob", f"{context}.kind is unsupported: {kind}", errors)

    model = entity.get("model")
    require(model in SUPPORTED_MODELS, f"{context}.model must be one of {sorted(SUPPORTED_MODELS)}", errors)
    texture = entity.get("texture", "")
    require(isinstance(texture, str) and bool(texture),
            f"{context}.texture must be a non-empty texture key", errors)
    if isinstance(texture, str) and texture:
        require(all(char in TEXTURE_KEY_CHARS for char in texture),
                f"{context}.texture must be a filename key, not a path: {texture}",
                errors)
        texture_path = mob_textures_dir / f"{texture}.png"
        require(texture_path.exists(),
                f"{context}.texture references a missing mob texture: {texture_path}",
                errors)
    scale = entity.get("scale", 1.0)
    require(isinstance(scale, (int, float)) and scale > 0,
            f"{context}.scale must be a positive number", errors)

    health = entity.get("health", 20)
    if isinstance(health, int):
        require(health > 0, f"{context}.health must be positive", errors)
    elif isinstance(health, dict):
        require(isinstance(health.get("current"), int) and health.get("current", 0) > 0,
                f"{context}.health.current must be a positive integer", errors)
        require(isinstance(health.get("max"), int) and health.get("max", 0) > 0,
                f"{context}.health.max must be a positive integer", errors)
    else:
        errors.append(f"{context}.health must be an integer or object")

    physics = entity.get("physics", {})
    if physics:
        require(isinstance(physics, dict), f"{context}.physics must be an object", errors)
        if isinstance(physics, dict):
            for key in ("halfExtents", "colliderOffset"):
                if key in physics:
                    validate_vec3(physics[key], f"{context}.physics.{key}", errors)
            if "eyeOffsetY" in physics:
                require(isinstance(physics["eyeOffsetY"], (int, float)),
                        f"{context}.physics.eyeOffsetY must be a number", errors)

    ai = entity.get("ai", {})
    require(isinstance(ai, dict), f"{context}.ai must be an object", errors)

    drops = entity.get("drops", [])
    require(isinstance(drops, list), f"{context}.drops must be an array", errors)
    if isinstance(drops, list):
        for drop_index, drop in enumerate(drops):
            drop_context = f"{context}.drops[{drop_index}]"
            if not isinstance(drop, dict):
                errors.append(f"{drop_context} must be an object")
                continue
            item = drop.get("item")
            require(isinstance(item, str) and item in known_items,
                    f"{drop_context}.item references an unknown item: {item}", errors)
            min_count = drop.get("min", 1)
            max_count = drop.get("max", 1)
            min_ok = isinstance(min_count, int) and min_count > 0
            max_ok = isinstance(max_count, int) and (not isinstance(min_count, int) or max_count >= min_count)
            require(min_ok, f"{drop_context}.min must be a positive integer", errors)
            require(max_ok, f"{drop_context}.max must be >= min", errors)

    death_effect = entity.get("deathEffect")
    if death_effect is not None:
        require(isinstance(death_effect, dict), f"{context}.deathEffect must be an object", errors)
        if isinstance(death_effect, dict):
            particle_block = death_effect.get("particleBlock", "")
            require(isinstance(particle_block, str),
                    f"{context}.deathEffect.particleBlock must be a string", errors)
            if isinstance(particle_block, str) and particle_block:
                require(particle_block in known_blocks,
                        f"{context}.deathEffect.particleBlock references an unknown block: {particle_block}",
                        errors)
            particle_count = death_effect.get("particleCount", 0)
            if particle_block:
                require(isinstance(particle_count, int) and particle_count > 0,
                        f"{context}.deathEffect.particleCount must be a positive integer", errors)
            elif "particleCount" in death_effect:
                require(isinstance(particle_count, int) and particle_count >= 0,
                        f"{context}.deathEffect.particleCount must be a non-negative integer", errors)
            sound = death_effect.get("sound", "")
            require(isinstance(sound, str), f"{context}.deathEffect.sound must be a string", errors)
            if isinstance(sound, str) and sound:
                require(sound in known_sounds,
                        f"{context}.deathEffect.sound references an unknown sound: {sound}", errors)
            volume = death_effect.get("volume", 1.0)
            require(isinstance(volume, (int, float)) and volume >= 0,
                    f"{context}.deathEffect.volume must be a non-negative number", errors)

    hurt_effect = entity.get("hurtEffect")
    if hurt_effect is not None:
        require(isinstance(hurt_effect, dict), f"{context}.hurtEffect must be an object", errors)
        if isinstance(hurt_effect, dict):
            sound = hurt_effect.get("sound", "")
            require(isinstance(sound, str), f"{context}.hurtEffect.sound must be a string", errors)
            if isinstance(sound, str) and sound:
                require(sound in known_sounds,
                        f"{context}.hurtEffect.sound references an unknown sound: {sound}", errors)
            volume = hurt_effect.get("volume", 1.0)
            require(isinstance(volume, (int, float)) and volume >= 0,
                    f"{context}.hurtEffect.volume must be a non-negative number", errors)
            flash_duration = hurt_effect.get("flashDurationSeconds", 0.18)
            require(isinstance(flash_duration, (int, float)) and flash_duration > 0,
                    f"{context}.hurtEffect.flashDurationSeconds must be a positive number", errors)


def validate_file(
    path: Path,
    items_path: Path,
    blocks_path: Path,
    sounds_path: Path,
    mob_textures_dir: Path,
) -> list[str]:
    data = load_json(path)
    entities = data.get("entities")
    errors: list[str] = []
    require(isinstance(entities, list), "root.entities must be an array", errors)
    if not isinstance(entities, list):
        return errors

    known_items = collect_known_item_ids(items_path, blocks_path)
    known_blocks = collect_known_block_ids(blocks_path)
    known_sounds = collect_known_sound_ids(sounds_path)
    seen_ids: set[str] = set()
    for index, entity in enumerate(entities):
        validate_entity(entity, index, known_items, known_blocks, known_sounds, mob_textures_dir, seen_ids, errors)
    return errors


def parse_drop(spec: str) -> dict[str, Any]:
    parts = spec.rsplit(":", 2)
    if len(parts) != 3:
        raise ValueError("drop must look like minecraft:item:min:max")
    item, min_text, max_text = parts
    return {"item": item, "min": int(min_text), "max": int(max_text)}


def scaffold_entity(args: argparse.Namespace) -> int:
    path: Path = args.path
    data = load_json(path) if path.exists() else {"entities": []}
    entities = data.setdefault("entities", [])
    if not isinstance(entities, list):
        print("root.entities must be an array", file=sys.stderr)
        return 1
    if any(isinstance(entity, dict) and entity.get("id") == args.entity_id for entity in entities):
        print(f"entity already exists: {args.entity_id}", file=sys.stderr)
        return 1

    drops = [parse_drop(drop) for drop in args.drop]
    entity = {
        "id": args.entity_id,
        "kind": "mob",
        "model": args.model,
        "texture": "zombie",
        "scale": 1.0,
        "health": {"current": args.health, "max": args.health},
        "eyeHeight": 1.62,
        "physics": {
            "halfExtents": [0.3, 0.9, 0.3],
            "colliderOffset": [0.0, 0.9, 0.0],
            "eyeOffsetY": 1.62,
        },
        "ai": {
            "wanderInterval": 3.0,
            "wanderSpeed": 0.45,
            "pursueSpeed": 0.85,
            "acquisitionRange": 14.0,
            "loseTargetRange": 20.0,
            "attackRange": 1.35,
            "attackCooldownSeconds": 1.1,
            "attackDamage": args.attack_damage,
        },
        "drops": drops,
        "hurtEffect": {
            "sound": "mob.zombie.hurt",
            "volume": 1.0,
            "flashDurationSeconds": 0.18,
        },
        "deathEffect": {
            "particleBlock": "minecraft:rose",
            "particleCount": 28,
            "sound": "mob.zombie.death",
            "volume": 1.0,
        },
    }
    entities.append(entity)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    print(f"added {args.entity_id} to {path}")
    return 0


def validate_command(args: argparse.Namespace) -> int:
    errors = validate_file(args.path, args.items, args.blocks, args.sounds, args.mob_textures)
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print(f"{args.path} is valid")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subcommands = parser.add_subparsers(dest="command", required=True)

    validate = subcommands.add_parser("validate", help="validate an entity definition file")
    validate.add_argument("path", nargs="?", type=Path, default=DEFAULT_ENTITIES_PATH)
    validate.add_argument("--items", type=Path, default=DEFAULT_ITEMS_PATH)
    validate.add_argument("--blocks", type=Path, default=DEFAULT_BLOCKS_PATH)
    validate.add_argument("--sounds", type=Path, default=DEFAULT_SOUNDS_PATH)
    validate.add_argument("--mob-textures", type=Path, default=DEFAULT_MOB_TEXTURES_DIR)
    validate.set_defaults(func=validate_command)

    scaffold = subcommands.add_parser("scaffold", help="append a default mob definition")
    scaffold.add_argument("entity_id", help="namespaced entity id, e.g. minecraft:pig")
    scaffold.add_argument("--path", type=Path, default=DEFAULT_ENTITIES_PATH)
    scaffold.add_argument("--model", choices=sorted(SUPPORTED_MODELS), default="humanoid")
    scaffold.add_argument("--health", type=int, default=20)
    scaffold.add_argument("--attack-damage", type=int, default=3)
    scaffold.add_argument("--drop", action="append", default=[],
                          help="drop entry like minecraft:coal:1:1; may be repeated")
    scaffold.set_defaults(func=scaffold_entity)
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
