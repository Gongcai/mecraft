#!/usr/bin/env python3
"""Convert Minecraft block model JSON files into Mecraft model JSON files."""

from __future__ import annotations

import argparse
import copy
import json
from pathlib import Path
from typing import Any


FACE_NAMES = {"down", "up", "north", "south", "west", "east"}


def normalize_model_path(value: str) -> str:
    """Return a model path relative to the Minecraft models directory."""
    path = value.replace("\\", "/")
    if ":" in path:
        path = path.split(":", 1)[1]
    if not path.endswith(".json"):
        path += ".json"
    return path


def read_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError(f"JSON root must be an object: {path}")
    return data


def load_model_with_inheritance(
    mc_root: Path,
    model_path: str,
    cache: dict[str, dict[str, Any]],
) -> dict[str, Any]:
    """Resolve parent inheritance using Minecraft's child-over-parent merge rule."""
    normalized_path = normalize_model_path(model_path)
    cached = cache.get(normalized_path)
    if cached is not None:
        return copy.deepcopy(cached)

    raw_path = mc_root / normalized_path
    raw = read_json(raw_path)

    merged: dict[str, Any] = {
        "textures": {},
        "elements": [],
        "ambientocclusion": True,
    }

    parent = raw.get("parent")
    if parent is not None:
        if not isinstance(parent, str):
            raise ValueError(f"Model parent must be a string: {raw_path}")
        parent_model = load_model_with_inheritance(mc_root, parent, cache)
        merged["textures"] = dict(parent_model.get("textures", {}))
        merged["elements"] = copy.deepcopy(parent_model.get("elements", []))
        merged["ambientocclusion"] = parent_model.get("ambientocclusion", True)

    textures = raw.get("textures")
    if textures is not None:
        if not isinstance(textures, dict):
            raise ValueError(f"Model textures must be an object: {raw_path}")
        merged["textures"].update(textures)

    if "elements" in raw:
        elements = raw["elements"]
        if not isinstance(elements, list):
            raise ValueError(f"Model elements must be an array: {raw_path}")
        merged["elements"] = copy.deepcopy(elements)

    if "ambientocclusion" in raw:
        if not isinstance(raw["ambientocclusion"], bool):
            raise ValueError(f"ambientocclusion must be boolean: {raw_path}")
        merged["ambientocclusion"] = raw["ambientocclusion"]

    cache[normalized_path] = copy.deepcopy(merged)
    return merged


def load_texture_map(path: Path | None) -> dict[str, str]:
    if path is None:
        return {}
    data = read_json(path)
    result: dict[str, str] = {}
    for key, value in data.items():
        if not isinstance(value, str):
            raise ValueError(f"Texture map value must be a string: {key}")
        result[key] = value
    return result


def remap_texture(value: str, texture_map: dict[str, str]) -> str:
    """Convert a Minecraft texture path into a Mecraft texture name."""
    if value.startswith("#"):
        return value
    name = value.replace("\\", "/")
    if ":" in name:
        name = name.split(":", 1)[1]
    if name.startswith("block/"):
        name = name[len("block/") :]
    return texture_map.get(name, name)


def fill_default_uv(element: dict[str, Any], face_name: str, face: dict[str, Any]) -> list[float]:
    existing = face.get("uv")
    if existing is not None:
        if not isinstance(existing, list) or len(existing) != 4:
            raise ValueError(f"Face uv must contain four numbers: {face_name}")
        return existing

    frm = element.get("from")
    to = element.get("to")
    if not isinstance(frm, list) or len(frm) != 3 or not isinstance(to, list) or len(to) != 3:
        raise ValueError("Element requires from/to before default uv can be computed")

    if face_name in ("down", "up"):
        return [frm[0], frm[2], to[0], to[2]]
    if face_name in ("north", "south"):
        return [frm[0], frm[1], to[0], to[1]]
    if face_name in ("west", "east"):
        return [frm[2], frm[1], to[2], to[1]]
    raise ValueError(f"Unknown face name: {face_name}")


def convert_model(mc_root: Path, input_path: str, texture_map: dict[str, str]) -> dict[str, Any]:
    cache: dict[str, dict[str, Any]] = {}
    merged = load_model_with_inheritance(mc_root, input_path, cache)

    raw_textures = merged.get("textures", {})
    if not isinstance(raw_textures, dict):
        raise ValueError(f"Merged textures must be an object: {input_path}")

    output: dict[str, Any] = {
        "format": "mecraft_model_v1",
        "ambientocclusion": bool(merged.get("ambientocclusion", True)),
        "textures": {
            key: remap_texture(value, texture_map)
            for key, value in raw_textures.items()
            if isinstance(value, str)
        },
        "elements": [],
    }

    elements = merged.get("elements")
    if not isinstance(elements, list):
        raise ValueError(f"Merged elements must be an array: {input_path}")

    for element in elements:
        if not isinstance(element, dict):
            raise ValueError(f"Model element must be an object: {input_path}")
        if "rotation" in element:
            raise ValueError(f"Element rotation is not supported by Mecraft model v1: {input_path}")

        new_element: dict[str, Any] = {
            "from": element["from"],
            "to": element["to"],
            "faces": {},
        }

        faces = element.get("faces", {})
        if not isinstance(faces, dict):
            raise ValueError(f"Element faces must be an object: {input_path}")
        for face_name, face in faces.items():
            if face_name not in FACE_NAMES:
                raise ValueError(f"Unknown face name in model: {face_name}")
            if not isinstance(face, dict):
                raise ValueError(f"Face must be an object: {face_name}")
            texture = face.get("texture")
            if not isinstance(texture, str):
                raise ValueError(f"Face texture must be a string: {face_name}")

            new_face: dict[str, Any] = {
                "texture": texture,
                "uv": fill_default_uv(element, face_name, face),
            }
            if "cullface" in face:
                cullface = face["cullface"]
                if not isinstance(cullface, str) or cullface not in FACE_NAMES:
                    raise ValueError(f"Face cullface is invalid: {face_name}")
                new_face["cullface"] = cullface
            if "rotation" in face:
                rotation = face["rotation"]
                if rotation not in (0, 90, 180, 270):
                    raise ValueError(f"Face rotation is invalid: {face_name}")
                new_face["rotation"] = rotation
            if "tintindex" in face:
                tintindex = face["tintindex"]
                if not isinstance(tintindex, int):
                    raise ValueError(f"Face tintindex must be integer: {face_name}")
                new_face["tintindex"] = tintindex
            new_element["faces"][face_name] = new_face

        output["elements"].append(new_element)

    return output


def output_name_for_input(input_path: str) -> str:
    normalized = normalize_model_path(input_path)
    return Path(normalized).name


def write_json(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=2, ensure_ascii=False)
        handle.write("\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mc-root", required=True, type=Path, help="Minecraft models directory")
    parser.add_argument("--input", required=True, nargs="+", help="Model path(s), for example block/oak_stairs.json")
    parser.add_argument("--output", type=Path, help="Output file for single-input conversion")
    parser.add_argument("--output-dir", type=Path, help="Output directory for one or more conversions")
    parser.add_argument("--texture-map", type=Path, help="Optional JSON map from Minecraft texture names to Mecraft names")
    args = parser.parse_args()

    mc_root = args.mc_root
    if not mc_root.is_dir():
        raise FileNotFoundError(f"Minecraft model root does not exist: {mc_root}")

    if args.output is not None and len(args.input) != 1:
        raise ValueError("--output can only be used with a single --input value")
    if args.output is None and args.output_dir is None:
        raise ValueError("Either --output or --output-dir is required")

    texture_map = load_texture_map(args.texture_map)
    for input_path in args.input:
        converted = convert_model(mc_root, input_path, texture_map)
        output_path = args.output if args.output is not None else args.output_dir / output_name_for_input(input_path)
        write_json(output_path, converted)
        print(f"converted {input_path} -> {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
