"""Local path configuration for the Pilot camera patch utility."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class CameraPaths:
    patch_map: Path
    writer: Path
    pilot: Path
    state_dir: Path
    transport_root: Path


def add_path_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--map", dest="map_path", type=Path,
                        help="local static patch description JSON")
    parser.add_argument("--writer", type=Path,
                        help="local AP flash writer artifact")
    parser.add_argument("--pilot", type=Path,
                        help="local Pilot DSO artifact")
    parser.add_argument("--state-dir", type=Path,
                        help="directory for local device operation records")
    parser.add_argument("--transport-root", type=Path,
                        help="directory containing cpload_run.py")


def resolve_paths(args: argparse.Namespace, repo_root: Path) -> CameraPaths:
    build = repo_root / "build"
    return CameraPaths(
        patch_map=args.map_path or repo_root / "patch.a6700.json",
        writer=args.writer or repo_root / "patch" / "pilot_ap_flash.so",
        pilot=args.pilot or build / "pilot.so",
        state_dir=args.state_dir or build / "device-state",
        transport_root=args.transport_root or repo_root / "patch",
    )
