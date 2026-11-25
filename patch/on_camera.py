#!/usr/bin/env python3
"""Manage Pilot and the configured AP and CP camera patches."""

from __future__ import annotations

import argparse
import hashlib
import importlib
import json
import os
import re
import sys
import tempfile
import time
from pathlib import Path

from on_camera_config import add_path_arguments, resolve_paths
from on_camera_profiles import classify, describe, source_profiles
from on_camera_profiles import validate as validate_profiles


PATCH_DIR = Path(__file__).resolve().parent
REPO_ROOT = PATCH_DIR.parent
MAP_PATH = REPO_ROOT / "patch.json"
PILOT_DSO_PATH = REPO_ROOT / "build" / "pilot.so"
FLASH_MODULE_PATH = REPO_ROOT / "patch" / "pilot_ap_flash.so"
STATE_DIR = REPO_ROOT / "build" / "device-state"
AP_PATH = "/tmp/jiritsu/../../usr/lib/appFw.so"
PILOT_DEVICE_PATH = "/tmp/jiritsu/../../usr/lib/pilot.so"
FLASH_FILES = {
    "trigger": "/tmp/jiritsu/libJit.so",
    "staged": "/tmp/jiritsu/pilotflash-staged.so",
    "mode": "/tmp/jiritsu/pilotflash.mode",
    "result": "/tmp/jiritsu/pilotflash.result",
    "plan": "/tmp/jiritsu/pilotflash-plan.bin",
    "pilot": "/tmp/jiritsu/pilotflash-pilot.so",
}
FLASH_HEADER = b"A6700PILOTFLASH\n"
FLASH_POLL_SECONDS = 120

# A map without a "runtime" block is the original, camera-verified A6700 2.00
# description; keep its exact paths and writer header.  Model-specific maps
# (for example patch.a7c2.json) carry a complete runtime block generated from
# models.json, so this utility needs no registry file of its own.
LEGACY_RUNTIME = {
    "model_id": "a6700",
    "camera": "Sony ILCE-6700",
    "firmware": "2.00",
    "dso_soname": "pilot.so",
    "dso_path": "/usr/lib/pilot.so",
    "boot_trace_path": "/tmp_wbshd/pilot-boot.bin",
    "marker_symbol": "pilot_a6700_v1_marker",
    "mount_callback_symbol": "pilot_a6700_v1_on_mount",
    "get_api_symbol": "pilot_a6700_get_api",
    "module_model_symbol": "pilot_module_model_a6700",
    "flash_header": "A6700PILOTFLASH\n",
    "foreign_model_ids": ["a7c2"],
    "foreign_marker_symbols": ["pilot_a7c2_v1_marker"],
}
RUNTIME_KEYS = frozenset(LEGACY_RUNTIME)
MODEL_RUNTIME = LEGACY_RUNTIME
MODEL_FLASH_HEADER = b"A6700PILOTFLASH\n"
PILOT_DSO_DISPLAY_PATH = LEGACY_RUNTIME["dso_path"]
PILOT_JIRITSU_PREFIX = "/tmp/jiritsu/../.."

def nul_strings(data: bytes) -> set[str]:
    """Printable NUL-terminated strings in an ELF (dynsym names included)."""
    return {chunk.decode("ascii") for chunk in data.split(b"\0")
            if 4 <= len(chunk) <= 96 and all(32 <= byte < 127 for byte in chunk)}


def resolve_runtime(patch_map: dict) -> dict:
    """Return the validated model identity carried by the patch map."""
    runtime = patch_map.get("runtime", LEGACY_RUNTIME)
    if not isinstance(runtime, dict) or set(runtime) != RUNTIME_KEYS:
        raise RuntimeError("patch map runtime block is incomplete; copy the full block from the model map")
    model_id = runtime["model_id"]
    if not isinstance(model_id, str) or not re.fullmatch(r"[a-z][a-z0-9_]{0,15}", model_id):
        raise RuntimeError("invalid runtime.model_id")
    dso_path, soname = runtime["dso_path"], runtime["dso_soname"]
    if not isinstance(dso_path, str) or \
            not re.fullmatch(r"/usr/lib/[A-Za-z0-9_.-]+\.so", dso_path) or \
            not isinstance(soname, str) or dso_path.rsplit("/", 1)[1] != soname:
        raise RuntimeError("invalid runtime.dso_path / dso_soname")
    for key in ("marker_symbol", "mount_callback_symbol", "get_api_symbol",
                "module_model_symbol"):
        symbol = runtime[key]
        if not isinstance(symbol, str) or \
                not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]{0,63}", symbol) or \
                model_id not in symbol:
            raise RuntimeError(f"runtime.{key} is not a {model_id} symbol")
    header = runtime["flash_header"]
    if not isinstance(header, str) or not header.endswith("\n") or len(header) > 32 or \
            any(ord(char) < 32 and char != "\n" or ord(char) > 126 for char in header):
        raise RuntimeError("invalid runtime.flash_header")
    for key in ("foreign_model_ids", "foreign_marker_symbols"):
        values = runtime[key]
        if not isinstance(values, list) or any(not isinstance(v, str) or not v for v in values):
            raise RuntimeError(f"invalid runtime.{key}")
    return runtime


def configure_model(patch_map: dict) -> dict:
    """Apply the map's model identity to device paths and writer defaults."""
    global MODEL_RUNTIME, MODEL_FLASH_HEADER, PILOT_DEVICE_PATH, PILOT_DSO_DISPLAY_PATH
    runtime = resolve_runtime(patch_map)
    MODEL_RUNTIME = runtime
    MODEL_FLASH_HEADER = runtime["flash_header"].encode("ascii")
    PILOT_DSO_DISPLAY_PATH = runtime["dso_path"]
    PILOT_DEVICE_PATH = PILOT_JIRITSU_PREFIX + runtime["dso_path"]
    return runtime


def validate_pilot_dso(pilot: bytes, patch_map: dict) -> None:
    """Refuse a DSO built for another body before it reaches the camera.

    Maps without a ``runtime`` block are the historical, camera-verified A6700
    descriptions; their DSO is accepted as before. Model-specific maps state the
    identity, so a mismatched or foreign DSO is refused here.
    """
    if not patch_map.get("runtime"):
        return
    runtime = resolve_runtime(patch_map)
    names = nul_strings(pilot)
    required = (runtime["marker_symbol"], runtime["mount_callback_symbol"],
                runtime["get_api_symbol"])
    missing = [symbol for symbol in required if symbol not in names]
    if missing:
        raise RuntimeError(
            f"Pilot DSO is not a {runtime['model_id']} build (missing {', '.join(missing)})")
    foreign = [symbol for symbol in runtime["foreign_marker_symbols"] if symbol in names]
    if foreign:
        raise RuntimeError(
            f"Pilot DSO exports foreign marker(s) {', '.join(foreign)}; "
            f"refusing to install it on a {runtime['model_id']} body")


def configure_flash_header(patch_map: dict) -> None:
    """Use a model-specific writer result header when --map supplies one.

    The model's runtime block sets the default. An explicit ``flash.header``
    overrides it, and a legacy A6700 map keeps the historical header.
    """
    global FLASH_HEADER
    flash = patch_map.get("flash")
    header = (flash.get("header", MODEL_FLASH_HEADER)
              if isinstance(flash, dict) else MODEL_FLASH_HEADER)
    if isinstance(header, str):
        try:
            header = header.encode("ascii")
        except UnicodeEncodeError as error:
            raise RuntimeError("flash.header must be ASCII") from error
    if (not isinstance(header, bytes) or not header or len(header) > 32 or
            b"\n" not in header):
        raise RuntimeError("flash.header must be a short ASCII line (<=32 bytes)")
    FLASH_HEADER = header


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def load_map() -> dict:
    patch_map = json.loads(MAP_PATH.read_text(encoding="utf-8-sig"))
    if patch_map.get("schema") != "pilot.patch-map.v1":
        raise RuntimeError("unsupported patch map")
    try:
        validate_profiles(patch_map)
    except ValueError as error:
        raise RuntimeError(f"invalid accepted_sources: {error}") from error
    return patch_map


def save_device_state(state: dict, state_dir: Path | None = None) -> None:
    if state_dir is None:
        state_dir = STATE_DIR
    state_dir.mkdir(parents=True, exist_ok=True)
    state_path = state_dir / "device-state.json"
    previous = {}
    if state_path.exists():
        previous = json.loads(state_path.read_text(encoding="utf-8-sig"))
        if not isinstance(previous, dict):
            raise RuntimeError("local device state must contain a JSON object")
    previous.update(state)
    payload = json.dumps(previous, indent=2, ensure_ascii=False) + "\n"
    fd, temporary_name = tempfile.mkstemp(prefix=".device-state-",
                                          suffix=".tmp", dir=state_dir)
    temporary_path = Path(temporary_name)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\r\n") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary_path, state_path)
    finally:
        temporary_path.unlink(missing_ok=True)


def backup_device_files(files: dict[str, bytes | None]) -> Path:
    """Preserve exact pre-write firmware bytes under local device state."""
    backup_root = STATE_DIR / "backups"
    backup_root.mkdir(parents=True, exist_ok=True)
    backup = Path(tempfile.mkdtemp(prefix="before-write-", dir=backup_root))
    records = {}
    for name, data in files.items():
        if Path(name).name != name or name in (".", ".."):
            raise ValueError("invalid device backup filename")
        records[name] = None if data is None else {"size_bytes": len(data), "sha256": sha256(data)}
        if data is not None:
            with (backup / name).open("xb") as stream:
                stream.write(data)
                stream.flush()
                os.fsync(stream.fileno())
    (backup / "metadata.json").write_text(
        json.dumps(records, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    save_device_state({"backup_dir": str(backup)})
    return backup


def load_transport(transport_root: Path):
    transport_root = transport_root.resolve()
    if not (transport_root / "cpload_run.py").is_file():
        raise FileNotFoundError(f"transport module not found under {transport_root}")
    module = sys.modules.get("cpload_run")
    if module is None:
        sys.path.insert(0, str(transport_root))
        module = importlib.import_module("cpload_run")
    module_path = getattr(module, "__file__", None)
    if module_path is None:
        raise RuntimeError("loaded cpload_run module has no source path")
    resolved_module_path = Path(module_path).resolve()
    if not resolved_module_path.is_relative_to(transport_root):
        raise RuntimeError(
            f"cpload_run was loaded from {resolved_module_path}, outside {transport_root}"
        )
    return module


def load_writer(patch_map: dict) -> bytes:
    writer = FLASH_MODULE_PATH.read_bytes()
    writer_meta = patch_map.get("flash_writer")
    if writer_meta is not None and (
            len(writer) != writer_meta.get("size_bytes") or
            sha256(writer) != writer_meta.get("sha256")):
        raise RuntimeError("AP writer differs from the artifact recorded in patch.json")
    if len(writer) > 65536:
        raise RuntimeError("AP writer exceeds the Jiritsu 64 KiB transfer limit")
    if len(writer) < 64 or writer[:4] != b"\x7fELF" or writer[4] != 2:
        raise RuntimeError("AP writer is not an ELF64 module")
    if int.from_bytes(writer[18:20], "little") != 183:
        raise RuntimeError("AP writer is not an AArch64 module")
    if b"pilot_ap_generic_v1\0" not in writer:
        raise RuntimeError("generic AP writer required")
    return writer


def load_pilot(patch_map: dict) -> bytes:
    pilot = PILOT_DSO_PATH.read_bytes()
    pilot_meta = patch_map.get("pilot_dso")
    if pilot_meta is not None and (
            len(pilot) != pilot_meta.get("size_bytes") or
            sha256(pilot) != pilot_meta.get("sha256")):
        raise RuntimeError("pilot.so differs from the DSO recorded in patch.json")
    if len(pilot) > 65536:
        raise RuntimeError(f"{resolve_runtime(patch_map)['dso_soname']} "
                           "exceeds the Jiritsu 64 KiB transfer limit")
    validate_pilot_dso(pilot, patch_map)
    return pilot


def parse_offset(value: int | str) -> int:
    return int(value, 0) if isinstance(value, str) else int(value)


def iter_patch_windows(patch_map: dict) -> list[tuple[str, int, bytes, bytes]]:
    windows: list[tuple[str, int, bytes, bytes]] = []
    for entry in patch_map.get("patches", []):
        offset = parse_offset(entry["file_offset"])
        length = int(entry["length"])
        original = bytes.fromhex(entry["original_bytes"])
        target = bytes.fromhex(entry["target_bytes"])
        if offset < 0 or length <= 0 or len(original) != length or len(target) != length:
            raise ValueError(f"invalid patch window: {entry.get('id', 'unknown')}")
        windows.append((str(entry["id"]), offset, original, target))
    return windows


def window_hashes(data: bytes, patch_map: dict) -> dict:
    windows = iter_patch_windows(patch_map)
    if not windows:
        raise ValueError("patch map contains no AP patch windows")
    actual_parts: list[bytes] = []
    baseline_parts: list[bytes] = []
    patched_parts: list[bytes] = []
    mismatches: list[dict] = []
    for name, offset, original, target in windows:
        length = len(original)
        if offset + length > len(data):
            raise ValueError(f"patch window {name} exceeds appFw.so size ({len(data)})")
        actual = data[offset:offset + length]
        actual_parts.append(actual)
        baseline_parts.append(original)
        patched_parts.append(target)
        if actual != original and actual != target:
            mismatches.append({
                "id": name,
                "offset": offset,
                "length": length,
                "actual": actual,
                "expected_original": original,
                "expected_target": target,
            })
    return {
        "actual_sha256": sha256(b"".join(actual_parts)),
        "baseline_sha256": sha256(b"".join(baseline_parts)),
        "patched_sha256": sha256(b"".join(patched_parts)),
        "mismatches": mismatches,
    }


def ap_state(data: bytes, patch_map: dict) -> str:
    target = patch_map["target"]
    if len(data) != target["binary_size"]:
        return "unknown"
    hashes = window_hashes(data, patch_map)
    if hashes["actual_sha256"] == hashes["baseline_sha256"]:
        return "baseline"
    if hashes["actual_sha256"] == hashes["patched_sha256"]:
        return "patched"
    return "unknown"


def camera_profile(data: bytes, patch_map: dict) -> str:
    try:
        return classify(data, patch_map)[1]
    except ValueError:
        return "unknown"


def read_ap(session: cr.Session) -> bytes:
    data = session.read_path(AP_PATH, missing=True)
    if data is None:
        raise RuntimeError("cannot read /usr/lib/appFw.so over Jiritsu")
    return data


def read_flash_files(session: cr.Session) -> dict[str, bytes | None]:
    return {
        key: session.read_path(path, missing=True)
        for key, path in FLASH_FILES.items()
    }


def remove_device_file(session: cr.Session, path: str) -> None:
    session.select(path)
    session.jiritsu(18, b"\0\xff", missing=True)


def classify_owned_files(files: dict[str, bytes | None], writer: bytes,
                         pilot: bytes | None = None, plan: bytes | None = None) -> list[str]:
    problems: list[str] = []
    for key, data in files.items():
        if data is None:
            continue
        if key in ("trigger", "staged") and data == writer:
            continue
        if key == "pilot" and pilot is not None and data == pilot:
            continue
        if key == "plan" and plan is not None and data == plan:
            continue
        if key == "mode" and data in (b"check\n", b"write\n", b"restore\n", b"pilot\n"):
            continue
        if key == "result" and len(data) <= 256 and data.startswith(FLASH_HEADER):
            continue
        problems.append(f"{FLASH_FILES[key]} contains an unrecognized file")
    return problems


def clean_owned_files(session: cr.Session, writer: bytes,
                      pilot: bytes | None = None, plan: bytes | None = None) -> None:
    files = read_flash_files(session)
    problems = classify_owned_files(files, writer, pilot, plan)
    if problems:
        raise RuntimeError("; ".join(problems))
    for key, data in files.items():
        if data is not None:
            remove_device_file(session, FLASH_FILES[key])
            print(f"removed owned file: {FLASH_FILES[key]}")


def ensure_no_flash_files(session: cr.Session) -> None:
    files = read_flash_files(session)
    present = [FLASH_FILES[key] for key, data in files.items() if data is not None]
    if present:
        raise RuntimeError(
            "Pilot flash staging files already exist; inspect them, then run --clean"
        )


def stage_pilot_dso(session: cr.Session, pilot: bytes,
                    patch_map: dict) -> bool:
    current = session.read_path(PILOT_DEVICE_PATH, missing=True)
    if current is not None and current != pilot:
        raise RuntimeError("existing /usr/lib/pilot.so differs from this build; inspect before replacing")
    print("persistent bootstrap: " + ("verified" if current == pilot else "will install in /usr/lib"))
    return current != pilot


def run_flash_module(session: cr.Session, writer: bytes, mode: str,
                     patch_map: dict | None = None, pilot: bytes | None = None) -> bytes:
    ensure_no_flash_files(session)
    staged_path = FLASH_FILES["staged"]
    mode_path = FLASH_FILES["mode"]
    trigger_path = FLASH_FILES["trigger"]

    inputs: dict[str, bytes] = {}
    if mode in ("write", "restore"):
        if patch_map is None:
            raise ValueError("AP patch map required")
        from prepare_ap import plan_from_config
        inputs["plan"] = plan_from_config(patch_map)
    if mode in ("write", "pilot"):
        if not pilot:
            raise ValueError("pilot.so required")
        inputs["pilot"] = pilot
    for key, data in inputs.items():
        session.select(FLASH_FILES[key])
        session.jiritsu(16, b"\0\xff\0" + data)
        if session.read_path(FLASH_FILES[key], missing=True) != data:
            raise RuntimeError(f"AP writer {key} staging readback mismatch")

    session.select(mode_path)
    session.jiritsu(16, b"\0\xff\0" + (mode + "\n").encode("ascii"))
    if session.read_path(mode_path, missing=True) != (mode + "\n").encode("ascii"):
        raise RuntimeError("AP writer mode staging readback mismatch")
    session.select(staged_path)
    session.jiritsu(16, b"\0\xff\0" + writer)
    if session.read_path(staged_path, missing=True) != writer:
        raise RuntimeError("AP writer staging readback mismatch")

    session.select(trigger_path)
    session.jiritsu(16, b"\0\xff\0" + writer)

    result: bytes | None = None
    for _ in range(FLASH_POLL_SECONDS):
        result = session.read_path(FLASH_FILES["result"], missing=True)
        if result is not None:
            break
        time.sleep(1.0)
    if result is None:
        raise RuntimeError(
            "AP writer produced no result; leave camera powered and preserve pilotflash files"
        )
    if not result.startswith(FLASH_HEADER):
        raise RuntimeError(f"unexpected AP writer result: {result[:32]!r}")

    for key in ("trigger", "staged", "mode"):
        data = session.read_path(FLASH_FILES[key], missing=True)
        if data is None:
            continue
        if key in ("trigger", "staged") and data != writer:
            raise RuntimeError(f"{FLASH_FILES[key]} changed during AP writer run")
        if key == "mode" and data != (mode + "\n").encode("ascii"):
            raise RuntimeError(f"{mode_path} changed during AP writer run")
        remove_device_file(session, FLASH_FILES[key])
    for key, expected in inputs.items():
        current = session.read_path(FLASH_FILES[key], missing=True)
        if current is not None and current != expected:
            raise RuntimeError(f"{FLASH_FILES[key]} changed during AP writer run")
        if current is not None:
            remove_device_file(session, FLASH_FILES[key])
    return result


def remove_result_if_matches(session: cr.Session, result: bytes) -> None:
    current = session.read_path(FLASH_FILES["result"], missing=True)
    if current == result:
        remove_device_file(session, FLASH_FILES["result"])


def print_result(data: bytes) -> None:
    print("AP writer result:")
    for line in data.decode("ascii", "replace").splitlines():
        print(f"  {line}")


def print_window_mismatch(data: bytes, patch_map: dict) -> None:
    if len(data) != patch_map["target"]["binary_size"]:
        print(f"WARNING: appFw.so size {len(data)} != expected {patch_map['target']['binary_size']}")
        return
    hashes = window_hashes(data, patch_map)
    print(f"WARNING: patch window hash mismatch:")
    print(f"  actual   windows sha256: {hashes['actual_sha256']}")
    print(f"  baseline windows sha256: {hashes['baseline_sha256']}")
    print(f"  patched  windows sha256: {hashes['patched_sha256']}")
    for item in hashes["mismatches"]:
        print(f"  differ: {item['id']} offset={hex(item['offset'])} length={item['length']}")


def with_actual_windows(patch_map: dict, data: bytes, mode: str) -> dict:
    """Return a copy of patch_map with actual on-camera window bytes for forced writes."""
    field = "original_bytes" if mode == "write" else "target_bytes"
    updated_patches = []
    for entry in patch_map["patches"]:
        offset = parse_offset(entry["file_offset"])
        length = int(entry["length"])
        copied = dict(entry)
        copied[field] = data[offset:offset + length].hex()
        updated_patches.append(copied)
    updated = dict(patch_map)
    updated["patches"] = updated_patches
    return updated


def record_camera_state(patch_map: dict, mode: str, before_hash: str,
                        after_hash: str, result: bytes,
                        profile: str = "unknown") -> None:
    applied = mode == "write"
    target = patch_map["target"]
    baseline_sha = target.get("binary_sha256")
    patched_sha = target.get("patched_binary_sha256")
    if (baseline_sha is None or patched_sha is None) and patch_map.get("patches"):
        windows = iter_patch_windows(patch_map)
        baseline_sha = baseline_sha or sha256(b"".join(orig for _, _, orig, _ in windows))
        patched_sha = patched_sha or sha256(b"".join(tgt for _, _, _, tgt in windows))
    save_device_state({
        "schema": "pilot.device-state.v1",
        "status": "applied_pending_power_cycle" if applied
                  else "restored_pending_power_cycle",
        "target": target.get("model"),
        "firmware": target.get("firmware"),
        "camera_applied": applied,
        "source_profiles": [item["profile"] for item in source_profiles(patch_map)],
        "camera_installation": {
            "last_operation": "install" if applied else "restore",
            "source_profile": profile,
            "baseline_sha256": baseline_sha,
            "patched_sha256": patched_sha,
            "before_sha256": before_hash,
            "after_sha256": after_hash,
            "writer_result": result.decode("ascii", "replace").strip(),
            "activation": "pending_full_power_cycle",
        },
    })


def run_check(session: cr.Session, patch_map: dict) -> int:
    data = read_ap(session)
    state = ap_state(data, patch_map)
    profile = camera_profile(data, patch_map)
    print(f"appFw.so: {len(data)} bytes sha256={sha256(data)}")
    if len(data) == patch_map["target"]["binary_size"] and patch_map.get("patches"):
        hashes = window_hashes(data, patch_map)
        print(f"actual windows sha256: {hashes['actual_sha256']}")
    if state == "unknown":
        print_window_mismatch(data, patch_map)
        print(f"accepted sources: {describe(patch_map)}")
        print(f"state: unknown profile={profile}; no files were written")
        return 3
    print(f"state: {state} profile={profile}")
    if state == "patched":
        bootstrap = session.read_path(PILOT_DEVICE_PATH, missing=True)
        if bootstrap is None:
            print("persistent bootstrap: missing /usr/lib/pilot.so")
            return 4
        digest = sha256(bootstrap)
        print(f"persistent bootstrap: {len(bootstrap)} bytes sha256={digest}")
        expected = patch_map.get("pilot_dso")
        if expected is not None and (
                len(bootstrap) != expected["size_bytes"] or digest != expected["sha256"]):
            print("persistent bootstrap: mismatch")
            return 4
    return 0


def run_write(session: cr.Session, patch_map: dict, writer: bytes,
              pilot: bytes, mode: str, force: bool = False) -> int:
    preload = session.read_path("/tmp/jiritsu/../../etc/ld.so.preload", missing=True)
    if preload:
        raise RuntimeError("/etc/ld.so.preload exists; refusing to invoke Jiritsu loader")

    before = read_ap(session)
    before_hash = sha256(before)
    state = ap_state(before, patch_map)
    before_profile = camera_profile(before, patch_map)
    print(f"before appFw: {len(before)} bytes sha256={before_hash} state={state} "
          f"profile={before_profile}")
    effective_map = patch_map
    if state == "unknown":
        print_window_mismatch(before, patch_map)
        print(f"accepted sources: {describe(patch_map)}")
        if len(before) != patch_map["target"]["binary_size"]:
            raise RuntimeError("appFw size does not match target binary_size")
        if not force:
            raise RuntimeError(
                "AP patch windows do not match expected baseline or patched hash; pass --force to override"
            )
        print("WARNING: --force enabled; proceeding with actual on-camera window bytes")
        effective_map = with_actual_windows(patch_map, before, mode)

    if mode == "write":
        ensure_no_flash_files(session)
        pilot_changed = stage_pilot_dso(session, pilot, patch_map)
        if state == "patched":
            print("install: appFw patch is already present")
            if pilot_changed:
                return run_update_pilot(session, patch_map, writer, pilot)
            return 0
    elif state == "baseline":
        print("restore: appFw is already at the recorded camera baseline")
        return 0

    backup_device_files({"appFw.so": before, "pilot.so": session.read_path(PILOT_DEVICE_PATH, missing=True)})
    result = run_flash_module(session, writer, mode, effective_map, pilot)
    print_result(result)
    after = read_ap(session)
    after_hash = sha256(after)
    after_state = ap_state(after, patch_map)
    print(f"after appFw:  {len(after)} bytes sha256={after_hash} state={after_state} "
          f"profile={camera_profile(after, patch_map)}")

    expected_state = "patched" if mode == "write" else "baseline"
    if b"RESULT=OK" not in result or after_state != expected_state:
        if after_hash == before_hash:
            print("AP bytes match the pre-run image; do not power-cycle")
        else:
            print("AP image changed unexpectedly; do not power-cycle; preserve result files")
        return 6

    if mode == "write":
        if session.read_path(PILOT_DEVICE_PATH, missing=True) != pilot:
            raise RuntimeError("persistent /usr/lib/pilot.so readback mismatch")
        pilot_state = {
            "schema": "pilot.device-state.v1",
            "last_operation": "install",
            "status": "applied_pending_power_cycle",
            "camera_installed_size_bytes": len(pilot),
            "camera_installed_sha256": sha256(pilot),
            "camera_staged_size_bytes": len(pilot),
            "camera_staged_sha256": sha256(pilot),
            "camera_stage_readback_verified": True,
            "camera_stage_activation": "not_confirmed",
            "camera_candidate_state": "staged_readback_verified_power_cycle_pending",
            "camera_persistent_path": "/usr/lib/pilot.so",
        }
        save_device_state(pilot_state)
        print(f"persistent bootstrap readback OK: {len(pilot)} bytes sha256={sha256(pilot)}")

    record_camera_state(patch_map, mode, before_hash, after_hash, result, before_profile)

    remove_result_if_matches(session, result)
    if mode == "write":
        print("INSTALL OK; fully power-cycle the camera to load the AP patch")
        print("restore uses the same explicit --map, --writer and --state-dir inputs with --restore --yes")
    else:
        print("RESTORE OK; fully power-cycle the camera to load the recorded baseline AP image")
    return 0


def run_update_pilot(session: cr.Session, patch_map: dict,
                     writer: bytes, pilot: bytes) -> int:
    preload = session.read_path("/tmp/jiritsu/../../etc/ld.so.preload", missing=True)
    if preload:
        raise RuntimeError("/etc/ld.so.preload exists; refusing to invoke Jiritsu loader")

    ensure_no_flash_files(session)
    current = session.read_path(PILOT_DEVICE_PATH, missing=True)
    if current == pilot:
        print(f"persistent /usr/lib/pilot.so already matches: {len(pilot)} bytes sha256={sha256(pilot)}")
        return 0

    backup_device_files({"pilot.so": current})
    result = run_flash_module(session, writer, "pilot", pilot=pilot)
    print_result(result)
    if b"RESULT=OK" not in result:
        return 6
    if session.read_path(PILOT_DEVICE_PATH, missing=True) != pilot:
        raise RuntimeError("persistent /usr/lib/pilot.so readback mismatch")

    save_device_state({
        "schema": "pilot.device-state.v1",
        "last_operation": "update-pilot",
        "status": "applied_pending_power_cycle",
        "camera_installed_size_bytes": len(pilot),
        "camera_installed_sha256": sha256(pilot),
        "camera_staged_size_bytes": len(pilot),
        "camera_staged_sha256": sha256(pilot),
        "camera_stage_readback_verified": True,
        "camera_stage_activation": "not_confirmed",
        "camera_candidate_state": "staged_readback_verified_power_cycle_pending",
        "camera_persistent_path": "/usr/lib/pilot.so",
    })
    remove_result_if_matches(session, result)
    print(f"PILOT UPDATE OK: {len(pilot)} bytes sha256={sha256(pilot)}")
    print("fully power-cycle the camera to load the updated /usr/lib/pilot.so")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--check", action="store_true", help="read-only AP patch window hash check")
    group.add_argument("--install", action="store_true", help="stage Pilot and install the AP patch")
    group.add_argument("--restore", action="store_true", help="restore the four original AP windows")
    group.add_argument("--update-pilot", action="store_true", help="update /usr/lib/pilot.so in place")
    group.add_argument("--clean", action="store_true", help="remove recognized Pilot flash staging files")
    group.add_argument("--prepare-ap", action="store_true", help="read AP and prepare a camera-specific patch and writer")
    group.add_argument("--cp-check", action="store_true", help="check configured CP patch")
    group.add_argument("--cp-install", action="store_true", help="install configured CP patch")
    group.add_argument("--cp-restore", action="store_true", help="restore configured CP patch")
    parser.add_argument("--yes", action="store_true", help="required for write operations")
    parser.add_argument("--force", action="store_true", help="allow AP install/restore when patch windows differ from expected")
    parser.add_argument("--output-dir", type=Path, help="new directory for the AP preparation")
    add_path_arguments(parser)
    args = parser.parse_args()

    if (args.install or args.restore or args.update_pilot or args.cp_install or args.cp_restore) and not args.yes:
        parser.error("write operations require --yes")
    paths = resolve_paths(args, REPO_ROOT)
    global MAP_PATH, PILOT_DSO_PATH, FLASH_MODULE_PATH, STATE_DIR
    MAP_PATH = paths.patch_map
    PILOT_DSO_PATH = paths.pilot
    FLASH_MODULE_PATH = paths.writer
    STATE_DIR = paths.state_dir
    if args.prepare_ap:
        if args.output_dir is None:
            parser.error("--prepare-ap requires --output-dir")
        import prepare_ap
        patch_map = load_map()
        pilot = PILOT_DSO_PATH.read_bytes()
        cr = load_transport(paths.transport_root)
        with cr.Session(skip_hasp=True) as session:
            image = read_ap(session)
        writer = FLASH_MODULE_PATH.read_bytes()
        result = prepare_ap.prepare(patch_map, image, pilot, args.output_dir, writer)
        if result["already_installed"]:
            print("AP patch windows already match the target; no preparation files created.")
        else:
            print(f"AP preparation: {result['directory']}")
        print(f"before sha256={result['before_sha256']}")
        print(f"after  sha256={result['after_sha256']}")
        for item in result["windows"]:
            print(f"  {item['id']} offset={item['file_offset']} length={item['length']} state={item['state']} differences={item['different_bytes']}")
            if item["state"] == "different":
                for diff in item["differences"][:16]:
                    print(f"    {diff['file_offset']}: actual={diff['actual']} declared={diff['declared_original']} target={diff['target']}")
                if len(item["differences"]) > 16 and not result["already_installed"]:
                    print("    More differences are listed in preflight-report.json")
        if not result["already_installed"]:
            print("Review preflight-report.json before choosing whether to install.")
        return 0
    if args.cp_check or args.cp_install or args.cp_restore:
        import cp_flash
        spec = cp_flash.load_config(MAP_PATH)
        cr = load_transport(paths.transport_root)
        action = "install" if args.cp_install else "restore" if args.cp_restore else "check"
        with cr.Session(skip_hasp=True) as session:
            cp_flash.run(session, spec, action, STATE_DIR / "cp-backups")
        return 0
    patch_map = load_map()
    runtime = configure_model(patch_map)
    if args.pilot is None:
        PILOT_DSO_PATH = REPO_ROOT / "build" / runtime["dso_soname"]
    configure_flash_header(patch_map)
    writer = None if args.check else load_writer(patch_map)
    pilot = load_pilot(patch_map) if (args.install or args.update_pilot) else b""

    print(f"target: {patch_map['target'].get('model', 'unknown')} "
          f"firmware {patch_map['target'].get('firmware', 'unknown')} "
          "/usr/lib/appFw.so")
    print(f"model: {runtime['model_id']}  DSO: {runtime['dso_path']}  "
          f"module tag: {runtime['module_model_symbol']}")
    if patch_map.get("patches"):
        windows = iter_patch_windows(patch_map)
        print(f"baseline windows sha256: {sha256(b''.join(orig for _, _, orig, _ in windows))}")
        print(f"patched  windows sha256: {sha256(b''.join(tgt for _, _, _, tgt in windows))}")
    if patch_map.get("accepted_sources"):
        print(f"accepted sources: {describe(patch_map)}")
    if writer is not None:
        print(f"AP writer: {len(writer)} bytes sha256={sha256(writer)}")
    if pilot:
        print(f"Pilot DSO: {len(pilot)} bytes sha256={sha256(pilot)}")

    cr = load_transport(paths.transport_root)
    with cr.Session(skip_hasp=True) as session:
        if args.check:
            return run_check(session, patch_map)
        if args.clean:
            assert writer is not None
            from prepare_ap import plan_from_config
            staged = read_flash_files(session)
            clean_pilot = load_pilot(patch_map) if staged["pilot"] is not None else None
            clean_plan = plan_from_config(patch_map) if staged["plan"] is not None else None
            clean_owned_files(session, writer, clean_pilot, clean_plan)
            return 0
        if args.update_pilot:
            assert writer is not None
            return run_update_pilot(session, patch_map, writer, pilot)
        assert writer is not None
        return run_write(session, patch_map, writer, pilot,
                         "write" if args.install else "restore", force=args.force)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
    except Exception as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(4)
