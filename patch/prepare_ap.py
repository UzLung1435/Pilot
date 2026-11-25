"""Prepare a per-camera AP patch plan without writing to the camera."""
from __future__ import annotations

import hashlib
import json
import struct
from pathlib import Path

WINDOW_IDS = ("post_fsys_mount_branch", "post_fsys_mount_dlopen_island",
              "pt_load0_p_filesz", "pt_load0_p_memsz")


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def encode_plan(windows: list[tuple[str, int, bytes, bytes]], image_size: int) -> bytes:
    if len(windows) != len(WINDOW_IDS) or not isinstance(image_size, int) or image_size <= 0:
        raise ValueError("four AP patch windows and an image size are required")
    seen = []
    for name, offset, before, after in windows:
        if (name != WINDOW_IDS[len(seen)] or not isinstance(offset, int) or offset < 0 or
                not before or len(before) > 4096 or len(before) != len(after) or offset + len(before) > image_size):
            raise ValueError(f"invalid AP patch window: {name}")
        if any(offset < end and start < offset + len(before) for start, end in seen):
            raise ValueError(f"overlapping AP patch window: {name}")
        seen.append((offset, offset + len(before)))
    header = struct.pack("<8sIIQ", b"PILOTAP1", 1, len(windows), image_size)
    table = b"".join(struct.pack("<QII", offset, len(before), 0) for _, offset, before, _ in windows)
    return header + table + b"".join(before + after for _, _, before, after in windows)


def plan_from_config(config: dict) -> bytes:
    entries = {entry["id"]: entry for entry in config["patches"]}
    if len(entries) != len(config["patches"]) or any(name not in entries for name in WINDOW_IDS):
        raise ValueError("AP patch map is missing required windows")
    windows = []
    for name in WINDOW_IDS:
        entry = entries[name]
        offset = int(entry["file_offset"], 0) if isinstance(entry["file_offset"], str) else entry["file_offset"]
        before, after = bytes.fromhex(entry["original_bytes"]), bytes.fromhex(entry["target_bytes"])
        if entry["length"] != len(before):
            raise ValueError(f"invalid patch length: {name}")
        windows.append((name, offset, before, after))
    return encode_plan(windows, config["target"]["binary_size"])


def prepare(config: dict, image: bytes, pilot: bytes, output: Path, writer_data: bytes) -> dict:
    if config.get("schema") != "pilot.patch-map.v1":
        raise ValueError("unsupported patch map")
    target = config["target"]
    if len(image) != target["binary_size"] or image[:4] != b"\x7fELF":
        raise ValueError("target AP image size or ELF header mismatch")
    if len(pilot) < 64 or len(pilot) > 65536 or pilot[:6] != b"\x7fELF\x02\x01" or int.from_bytes(pilot[18:20], "little") != 183:
        raise ValueError("pilot.so must be an AArch64 ELF64 under 64 KiB")
    entries = {entry["id"]: entry for entry in config["patches"]}
    if len(entries) != len(config["patches"]) or any(name not in entries for name in WINDOW_IDS):
        raise ValueError("AP patch map is missing required windows")
    ranges, windows, report = [], [], []
    candidate = bytearray(image)
    for name in WINDOW_IDS:
        entry = entries[name]
        offset = int(entry["file_offset"], 0) if isinstance(entry["file_offset"], str) else entry["file_offset"]
        length = entry["length"]
        expected = bytes.fromhex(entry["original_bytes"])
        after = bytes.fromhex(entry["target_bytes"])
        if not isinstance(offset, int) or not isinstance(length, int) or offset < 0 or length <= 0 or length > 4096 or offset + length > len(image) or len(expected) != length or len(after) != length:
            raise ValueError(f"invalid patch window: {name}")
        if any(offset < end and start < offset + length for start, end in ranges):
            raise ValueError(f"overlapping patch window: {name}")
        ranges.append((offset, offset + length))
        before = image[offset:offset + length]
        changed = [i for i, (a, b) in enumerate(zip(before, expected)) if a != b]
        report.append({"id": name, "file_offset": hex(offset), "length": length,
                       "state": "original" if before == expected else "target" if before == after else "different",
                       "different_bytes": len(changed),
                       "differences": [{"file_offset": hex(offset + i), "actual": f"{before[i]:02x}",
                                        "declared_original": f"{expected[i]:02x}", "target": f"{after[i]:02x}"} for i in changed]})
        windows.append((name, offset, before, after))
        candidate[offset:offset + length] = after
    if candidate == image:
        return {"already_installed": True, "before_sha256": digest(image),
                "after_sha256": digest(candidate), "windows": report}
    if (len(writer_data) < 64 or len(writer_data) > 65536 or writer_data[:6] != b"\x7fELF\x02\x01" or
            int.from_bytes(writer_data[18:20], "little") != 183 or b"pilot_ap_generic_v1\0" not in writer_data):
        raise ValueError("generic AArch64 AP writer required")
    output.mkdir(parents=True, exist_ok=False)
    (output / "appFw.before.so").write_bytes(image)
    (output / "pilot.so").write_bytes(pilot)
    (output / "pilot_ap_flash.so").write_bytes(writer_data)
    (output / "patch.bin").write_bytes(encode_plan(windows, len(image)))
    plan = {"schema": "pilot.patch-map.v1", "target": {"model": target["model"], "firmware": target["firmware"],
            "binary_size": len(image), "binary_sha256": digest(image), "patched_binary_sha256": digest(candidate)},
            "pilot_dso": {"size_bytes": len(pilot), "sha256": digest(pilot)},
            "flash_writer": {"size_bytes": len(writer_data), "sha256": digest(writer_data)},
            "patches": [{"id": n, "file_offset": hex(o), "length": len(b), "original_bytes": b.hex(), "target_bytes": a.hex()} for n, o, b, a in windows]}
    (output / "patch.json").write_text(json.dumps(plan, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (output / "preflight-report.json").write_text(json.dumps({"windows": report, "before_sha256": digest(image), "after_sha256": digest(candidate)}, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return {"already_installed": False, "directory": str(output.resolve()), "before_sha256": digest(image), "after_sha256": digest(candidate), "windows": report}
