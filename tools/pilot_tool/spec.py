"""Immutable, versioned patch descriptions; no USB or workspace imports."""
from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path


def exact_keys(value, required, optional=()):
    if not isinstance(value, dict) or set(value) - set(required) - set(optional) or set(required) - set(value):
        raise ValueError(f"expected fields {sorted(required)}; optional {sorted(optional)}")


def integer(value):
    if isinstance(value, bool):
        raise ValueError("boolean is not an address")
    if isinstance(value, str) and re.fullmatch(r"0x[0-9a-fA-F]+", value):
        value = int(value, 16)
    if not isinstance(value, int) or not 0 <= value < 2**64:
        raise ValueError("expected unsigned 64-bit integer or hex address")
    return value


def hex_bytes(value):
    if not isinstance(value, str) or not re.fullmatch(r"(?:[0-9a-fA-F]{2})+", value):
        raise ValueError("bytes must be non-empty hex pairs without spaces")
    return bytes.fromhex(value)


def load_spec(path: Path):
    return validate_spec(json.loads(path.read_text(encoding="utf-8-sig")))


def validate_spec(spec):
    exact_keys(spec, {"schema", "id", "kind", "target", "patches"})
    if spec["schema"] != "pilot.patch.v1" or spec["kind"] not in ("file", "ram"):
        raise ValueError("expected pilot.patch.v1, kind=file or ram")
    if not isinstance(spec["id"], str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,63}", spec["id"]):
        raise ValueError("invalid patch id")
    target = spec["target"]
    fields = {"model", "firmware", "component", "sha256", "size_bytes"}
    if spec["kind"] == "file":
        fields.add("patched_sha256")
    exact_keys(target, fields)
    for key in ("model", "firmware", "component"):
        if not isinstance(target[key], str) or not target[key] or len(target[key]) > 100:
            raise ValueError(f"invalid target {key}")
    for key in ("sha256", "patched_sha256"):
        if key in target and (not isinstance(target[key], str) or not re.fullmatch(r"[0-9a-f]{64}", target[key])):
            raise ValueError(f"invalid {key}")
    if integer(target["size_bytes"]) == 0 or not isinstance(target["size_bytes"], int):
        raise ValueError("size_bytes must be a positive integer")
    if spec["kind"] == "ram" and target["component"] != "appFw.so":
        raise ValueError("RAM modules currently identify /usr/lib/appFw.so")
    patches = spec["patches"]
    if not isinstance(patches, list) or not 1 <= len(patches) <= 128:
        raise ValueError("patches must contain 1..128 entries")
    ids, ranges = set(), []
    for patch in patches:
        position = "offset" if spec["kind"] == "file" else "address"
        fields = {"id", position, "before", "after"}
        if spec["kind"] == "ram":
            fields.add("space")
        exact_keys(patch, fields)
        if not isinstance(patch["id"], str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,63}", patch["id"]) or patch["id"] in ids:
            raise ValueError("invalid or duplicate site id")
        ids.add(patch["id"])
        before, after = hex_bytes(patch["before"]), hex_bytes(patch["after"])
        if len(before) != len(after) or before == after:
            raise ValueError("before/after must have equal lengths and change bytes")
        address = integer(patch[position])
        end = address + len(before)
        space = patch.get("space", "file")
        if end >= 2**64 or (spec["kind"] == "file" and end > target["size_bytes"]):
            raise ValueError("patch window outside target")
        if spec["kind"] == "ram":
            if space not in ("ap", "cp") or len(before) > 64:
                raise ValueError("RAM site requires ap/cp and at most 64 bytes")
            if space == "cp" and address < 0x01000000:
                if address < 0x00128000 or end > 0x011D8000 or (address < 0x00C12FC0 and end > 0x00C12D68):
                    raise ValueError("CP patch outside supported code region")
        if any(s == space and address < e and start < end for s, start, e in ranges):
            raise ValueError("overlapping patch windows")
        ranges.append((space, address, end))
    return spec


def patch_image(spec, data: bytes, restore=False):
    validate_spec(spec)
    if spec["kind"] != "file":
        raise ValueError("file patch description required")
    target = spec["target"]
    source_hash = target["patched_sha256" if restore else "sha256"]
    result_hash = target["sha256" if restore else "patched_sha256"]
    if len(data) != target["size_bytes"] or hashlib.sha256(data).hexdigest() != source_hash:
        raise ValueError("input image size or full SHA-256 mismatch")
    for site in spec["patches"]:
        offset = integer(site["offset"])
        expected = hex_bytes(site["after" if restore else "before"])
        if data[offset:offset + len(expected)] != expected:
            raise ValueError(f"site {site['id']}: expected bytes mismatch")
    output = bytearray(data)
    for site in spec["patches"]:
        offset = integer(site["offset"])
        replacement = hex_bytes(site["before" if restore else "after"])
        output[offset:offset + len(replacement)] = replacement
    if hashlib.sha256(output).hexdigest() != result_hash:
        raise ValueError("patched image SHA-256 mismatch")
    return bytes(output)
