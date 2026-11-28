#!/usr/bin/env python3
"""Build the `cp.patch` entry for ILCE-7CM2 2.01 from a stock cpapp-b.bin.

Usage:
    python patch/cp/a7c2_uipc_v4/gen_patch.py --cpapp <cpapp-b.bin> --out <cp.json>

The two payloads come from `build.sh` in this directory:
    patch.bin   868 B  -> written at VA 0x00C63EA4 (file 0x00B5BEA4)
    branch.bin    4 B  -> replaces the hook at file 0x003ABB0A

Every window is verified against the stock image before the spec is written, so
running this against a different firmware revision fails loudly instead of
producing a patch that would corrupt CP.
"""

import argparse
import hashlib
import json
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent

HOOK_OFF = 0x003ABB0A
HOOK_VA = 0x004B3B0A
HOOK_BEFORE = bytes.fromhex("307818b1")
CAVE_OFF = 0x00B5BEA4
CAVE_VA = 0x00C63EA4

TARGET = {
    "model": "Sony ILCE-7CM2 (A7C II)",
    "firmware": "2.01",
    "component": "cpapp-b.bin",
}


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cpapp", required=True, type=pathlib.Path,
                        help="stock cpapp-b.bin of ILCE-7CM2 2.01")
    parser.add_argument("--out", required=True, type=pathlib.Path,
                        help="where to write the cp.patch JSON")
    parser.add_argument("--image-out", type=pathlib.Path, default=None,
                        help="also write the fully patched cpapp-b.bin here")
    args = parser.parse_args()

    image = bytearray(args.cpapp.read_bytes())
    patch = (HERE / "patch.bin").read_bytes()
    branch = (HERE / "branch.bin").read_bytes()

    if branch != bytes.fromhex("b0f3cbb1"):
        print(f"branch.bin must be b0f3cbb1, got {branch.hex()}", file=sys.stderr)
        return 1
    if bytes(image[HOOK_OFF:HOOK_OFF + 4]) != HOOK_BEFORE:
        print(f"hook site {HOOK_VA:#x} does not hold 307818b1", file=sys.stderr)
        return 1
    cave = image[CAVE_OFF:CAVE_OFF + len(patch)]
    if any(cave):
        print(f"cave {CAVE_VA:#x} is not empty", file=sys.stderr)
        return 1

    original_sha = digest(image)

    image[HOOK_OFF:HOOK_OFF + 4] = branch
    image[CAVE_OFF:CAVE_OFF + len(patch)] = patch
    patched_sha = digest(image)

    spec = {
        "schema": "pilot.patch.v1",
        "id": "a7c2-cp-uipc-v4",
        "kind": "file",
        "target": dict(TARGET, sha256=original_sha,
                       patched_sha256=patched_sha, size_bytes=len(image)),
        "patches": [
            {"id": "receiver-hook", "offset": HOOK_OFF,
             "before": HOOK_BEFORE.hex(), "after": branch.hex()},
            {"id": "uipc-v4-handler", "offset": CAVE_OFF,
             "before": bytes(len(patch)).hex(), "after": patch.hex()},
        ],
    }

    args.out.write_text(json.dumps(spec, indent=2) + "\n", encoding="utf-8")
    if args.image_out is not None:
        args.image_out.write_bytes(image)
    print(f"hook   {HOOK_VA:#010x} {HOOK_BEFORE.hex()} -> {branch.hex()}")
    print(f"cave   {CAVE_VA:#010x} {len(patch)} bytes")
    print(f"sha256 {original_sha}")
    print(f"       -> {patched_sha}")
    print(f"wrote  {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
