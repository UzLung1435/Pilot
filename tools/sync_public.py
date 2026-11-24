#!/usr/bin/env python3
"""Copy only reviewed public source paths into a separate local checkout."""
from __future__ import annotations

import argparse
import json
from pathlib import Path


def sync(destination: Path, write=False):
    source = Path(__file__).resolve().parents[1]
    destination = destination.resolve()
    if destination == source or source in destination.parents or destination in source.parents:
        raise ValueError("public checkout must be separate from the research checkout")
    if not (destination / ".git").exists():
        raise ValueError("destination must be an existing Git checkout")
    files = json.loads((source / "tools/public-files.json").read_text(encoding="utf-8"))
    if not isinstance(files, list) or len(files) != len(set(files)):
        raise ValueError("invalid public source list")
    # Public sources normally live under tools/; a short allowlist covers the
    # standalone camera-side entry point and the model registry that both
    # deploy.py and on_camera.py rely on.
    allowed_other = {"patch/on_camera.py", "patch/on_camera_config.py",
                     "patch/tests/test_on_camera_offline.py", "models.json"}
    changes = []
    for relative in files:
        path = Path(relative)
        if path.is_absolute() or ".." in path.parts or "\\" in relative or not (relative.startswith("tools/") or relative in allowed_other):
            raise ValueError(f"not a public tool source path: {relative}")
        if path.suffix not in {".py", ".c", ".h", ".md", ".json"}:
            raise ValueError(f"not a source file: {relative}")
        src, dst = source / path, destination / path
        if src.is_symlink() or not src.is_file() or not src.resolve().is_relative_to(source):
            raise ValueError(f"invalid public source: {relative}")
        if not dst.resolve().is_relative_to(destination):
            raise ValueError(f"public output escapes checkout: {relative}")
        data = src.read_bytes()
        data.decode("utf-8-sig")
        if not dst.exists() or dst.read_bytes() != data:
            changes.append((relative, data))
    for relative, data in changes:
        if write:
            dst = destination / relative
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_bytes(data)
        print(("copied " if write else "differs ") + relative)
    return len(changes)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path, required=True)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    try:
        different = sync(args.destination, args.write)
    except (ValueError, OSError) as error:
        parser.exit(2, f"ERROR: {error}\n")
    raise SystemExit(0 if args.write or not different else 1)
