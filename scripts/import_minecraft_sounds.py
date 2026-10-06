#!/usr/bin/env python3
"""Import only the Minecraft Java sound assets used by Mecraft."""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
from urllib.request import Request, urlopen


VERSION_MANIFEST_URL = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"
RESOURCE_BASE_URL = "https://resources.download.minecraft.net"
SOUND_EVENT_MAP = {
    "player.step.grass": "block.grass.step",
    "player.land.small": "entity.player.small_fall",
    "player.land.big": "entity.player.big_fall",
    "player.hurt.classic": "entity.player.hurt",
    "block.generic.break": "block.stone.break",
    "block.generic.place": "block.stone.place",
    "block.stone.break": "block.stone.break",
    "block.stone.place": "block.stone.place",
    "block.grass.break": "block.grass.break",
    "block.grass.place": "block.grass.place",
    "block.gravel.break": "block.gravel.break",
    "block.gravel.place": "block.gravel.place",
    "block.wood.break": "block.wood.break",
    "block.wood.place": "block.wood.place",
    "block.sand.break": "block.sand.break",
    "block.sand.place": "block.sand.place",
    "block.sand.fall": "block.sand.fall",
    "block.glass.break": "block.glass.break",
    "block.glass.place": "block.glass.place",
    "block.metal.break": "block.metal.break",
    "block.metal.place": "block.metal.place",
    "block.note_block.harp": "block.note_block.harp",
    "item.apple.throw": "entity.egg.throw",
    "item.apple.impact": "entity.arrow.hit",
    "item.bucket.fill": "item.bucket.fill",
    "item.bucket.empty": "item.bucket.empty",
    "item.hoe.till": "item.hoe.till",
    "mob.zombie.hurt": "entity.zombie.hurt",
    "mob.zombie.death": "entity.zombie.death",
    "mob.creeper.hurt": "entity.creeper.hurt",
    "mob.creeper.death": "entity.creeper.death",
    "mob.cow.hurt": "entity.cow.hurt",
    "mob.cow.death": "entity.cow.death",
    "mob.pig.hurt": "entity.pig.hurt",
    "mob.pig.death": "entity.pig.death",
    "mob.sheep.hurt": "entity.sheep.hurt",
    "mob.sheep.death": "entity.sheep.death",
}


def fetch_bytes(url):
    """Fetch one resource from Mojang's public metadata or asset endpoint."""
    request = Request(url, headers={"User-Agent": "MecraftMinecraftSoundImporter/1.0"})
    with urlopen(request, timeout=30) as response:
        return response.read()


def fetch_json(url):
    """Fetch and parse one Mojang JSON resource."""
    return json.loads(fetch_bytes(url))


def fetch_object(object_hash, expected_size=None):
    """Fetch one content-addressed asset and verify Mojang's SHA-1 hash."""
    url = f"{RESOURCE_BASE_URL}/{object_hash[:2]}/{object_hash}"
    payload = fetch_bytes(url)
    actual_hash = hashlib.sha1(payload).hexdigest()
    if actual_hash != object_hash:
        raise ValueError(f"Mojang asset hash mismatch: {object_hash} != {actual_hash}")
    if expected_size is not None and len(payload) != expected_size:
        raise ValueError(f"Mojang asset size mismatch for {object_hash}")
    return payload


def normalize_event_id(event_id):
    """Remove the Minecraft namespace prefix from an event reference."""
    if ":" in event_id:
        namespace, event_path = event_id.split(":", 1)
        if namespace != "minecraft":
            raise ValueError(f"Unsupported sound namespace: {namespace}")
        return event_path
    return event_id


def resolve_event(event_id, definitions, stack=()):
    """Resolve an event into weighted files and event-level playback controls."""
    event_id = normalize_event_id(event_id)
    if event_id in stack:
        raise ValueError(f"Sound event reference cycle: {' -> '.join(stack + (event_id,))}")
    event = definitions.get(event_id)
    if not isinstance(event, dict) or not isinstance(event.get("sounds"), list):
        raise ValueError(f"Minecraft sound event is missing or invalid: {event_id}")

    event_volume = float(event.get("volume", 1.0))
    event_pitch = float(event.get("pitch", 1.0))
    variants = []
    for item in event["sounds"]:
        sound = {"name": item} if isinstance(item, str) else item
        if not isinstance(sound, dict) or not isinstance(sound.get("name"), str):
            raise ValueError(f"Invalid sound variant in Minecraft event: {event_id}")

        weight = max(0, int(sound.get("weight", 1)))
        volume = float(sound.get("volume", 1.0))
        pitch = float(sound.get("pitch", 1.0))
        name = sound["name"]
        if sound.get("type", "sound") == "event":
            nested_volume, nested_pitch, nested_variants = resolve_event(
                name, definitions, stack + (event_id,)
            )
            for nested in nested_variants:
                variants.append(
                    {
                        "name": nested["name"],
                        "weight": weight * nested["weight"],
                        "volume": volume * nested_volume * nested["volume"],
                        "pitch": pitch * nested_pitch * nested["pitch"],
                    }
                )
        else:
            variants.append({"name": name, "weight": weight, "volume": volume, "pitch": pitch})

    if not variants:
        raise ValueError(f"Minecraft sound event has no variants: {event_id}")
    return event_volume, event_pitch, variants


def normalize_asset_name(name):
    """Validate a sound sample path and return its indexed OGG asset key."""
    if ":" in name:
        namespace, name = name.split(":", 1)
        if namespace != "minecraft":
            raise ValueError(f"Unsupported sound asset namespace: {namespace}")
    sample_path = PurePosixPath(name)
    if sample_path.is_absolute() or ".." in sample_path.parts:
        raise ValueError(f"Unsafe Minecraft sound path: {name}")
    if sample_path.suffix == "":
        sample_path = sample_path.with_suffix(".ogg")
    if sample_path.suffix != ".ogg":
        raise ValueError(f"Unsupported Minecraft sound format: {sample_path}")
    return sample_path.as_posix()


def resolve_version(version):
    """Resolve a game version to the official asset index and sound definitions."""
    version_manifest = fetch_json(VERSION_MANIFEST_URL)
    version_info = next((item for item in version_manifest["versions"] if item["id"] == version), None)
    if version_info is None:
        raise ValueError(f"Minecraft version is not in Mojang's manifest: {version}")

    version_metadata = fetch_json(version_info["url"])
    asset_index = fetch_json(version_metadata["assetIndex"]["url"])
    sound_object = asset_index["objects"].get("minecraft/sounds.json")
    if sound_object is None:
        raise ValueError(f"The Minecraft {version} asset index has no sounds.json")
    sound_bytes = fetch_object(sound_object["hash"], sound_object["size"])
    return asset_index["objects"], json.loads(sound_bytes)


def main():
    """Download mapped samples and write the local vanilla sound overlay."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", default="1.21.1", help="Minecraft Java version (default: 1.21.1)")
    args = parser.parse_args()

    repository_root = Path(__file__).resolve().parent.parent
    sounds_root = repository_root / "assets" / "sounds"
    object_index, definitions = resolve_version(args.version)
    catalog = {"version": 1, "sounds": {}}
    downloads = {}

    for project_event, minecraft_event in SOUND_EVENT_MAP.items():
        event_volume, event_pitch, variants = resolve_event(minecraft_event, definitions)
        output_variants = []
        for variant in variants:
            asset_name = normalize_asset_name(variant["name"])
            asset_key = f"minecraft/sounds/{asset_name}"
            asset = object_index.get(asset_key)
            if asset is None:
                raise ValueError(f"Sound sample is missing from Mojang's asset index: {asset_key}")
            local_name = f"vanilla/{asset_name}"
            downloads[local_name] = asset
            output_variants.append(
                {
                    "path": local_name,
                    "weight": variant["weight"],
                    "volume": variant["volume"],
                    "pitch": variant["pitch"],
                }
            )
        catalog["sounds"][project_event] = {
            "variants": output_variants,
            "volume": event_volume,
            "pitch": event_pitch,
        }

    for local_name, asset in downloads.items():
        destination = sounds_root / local_name
        destination.parent.mkdir(parents=True, exist_ok=True)
        if destination.is_file():
            existing_hash = hashlib.sha1(destination.read_bytes()).hexdigest()
            if existing_hash == asset["hash"]:
                continue

        payload = fetch_object(asset["hash"], asset["size"])
        temporary_path = destination.with_suffix(destination.suffix + ".part")
        temporary_path.write_bytes(payload)
        temporary_path.replace(destination)

    catalog_path = sounds_root / "vanilla" / "sounds.json"
    catalog_path.write_text(json.dumps(catalog, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"已从 Mojang 官方资源导入 Minecraft Java {args.version} 音效：{len(downloads)} 个 OGG 文件")
    print(f"运行时清单：{catalog_path.relative_to(repository_root)}")


if __name__ == "__main__":
    main()
