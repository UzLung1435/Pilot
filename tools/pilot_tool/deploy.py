"""Safe local deployment and log export helpers for public Pilot modules."""

from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from .models import model_ids, module_model_symbol

_NAME = re.compile(r"^[A-Za-z0-9_.-]{1,64}\.so$")
_LOGS = ("log.txt",)
_MODEL_TAG = re.compile(rb"pilot_module_model_([a-z][a-z0-9_]{0,15})\x00")
# The camera Fsys validates extensions and rejects .json opens with 0x80000041,
# so the manifest carries JSON content under a .txt name.  cards written by
# older tool versions still have modules.json and are migrated on next deploy.
_MANIFEST = "modules.txt"
_LEGACY_MANIFESTS = ("modules.json",)


def _no_symlink(path: Path, *, allow_missing: bool = False) -> None:
    """Reject symlinks in every existing path component."""
    absolute = Path(os.path.abspath(path))
    current = Path(absolute.anchor)
    for part in absolute.parts[1:]:
        current /= part
        if current.is_symlink():
            raise ValueError(f"symlink path is not allowed: {current}")
        if not current.exists() and not allow_missing:
            raise FileNotFoundError(current)


def _safe_card_file(card: Path, relative: str, *, optional: bool = False) -> Path:
    card = Path(card)
    if not card.is_absolute():
        raise ValueError("card root must be an explicit absolute path")
    _no_symlink(card)
    target = card / relative
    _no_symlink(target, allow_missing=optional)
    if target.exists() and not target.is_file():
        raise ValueError(f"expected a regular file: {target}")
    return target


def _module_name(name: str) -> bool:
    return bool(len(name) <= 64 and _NAME.fullmatch(name) and not name.startswith(".") and ".." not in name and "/" not in name and "\\" not in name)


def _manifest_file(card: Path, *, optional: bool) -> Path:
    """Return the card manifest path, preferring the Fsys-safe .txt name.

    A card may still carry the legacy modules.json; it is read so the next
    deploy migrates the contents instead of dropping the existing module list.
    """
    for name in (_MANIFEST, *_LEGACY_MANIFESTS):
        path = _safe_card_file(card, f"pilot/{name}", optional=True)
        if path.exists():
            return path
    return _safe_card_file(card, f"pilot/{_MANIFEST}", optional=optional)


def _read_manifest(path: Path) -> list[str]:
    if not path.exists():
        return []
    try:
        raw = path.read_text(encoding="utf-8")
        data = json.loads(raw)
    except (UnicodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"invalid UTF-8 JSON manifest: {path}") from exc
    # Match the loader's deliberately strict grammar: exactly one object key,
    # a modules array, and no unrecognized fields or trailing JSON values.
    if not isinstance(data, dict) or set(data) != {"modules"} or not isinstance(data["modules"], list):
        raise ValueError("manifest must contain only a modules array")
    names = data["modules"]
    if len(names) > 16 or any(not isinstance(n, str) or not _module_name(n) for n in names):
        raise ValueError("manifest modules must be valid .so names, at most 16")
    return names


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _check_module_model(data: bytes, name: str, model: str | None) -> str | None:
    """Reject a module whose exported model tag (or filename) is for another body.

    The tag lives in .dynstr, so a NUL-terminated scan also works on stripped
    modules without section headers.  The camera-side framework performs the
    authoritative check with dlsym() before pilot_module_init().
    """
    if model is None:
        return None
    known = model_ids()
    if model not in known:
        raise ValueError(f"unknown model {model!r}; registered: {', '.join(known)}")
    expected = module_model_symbol(model)
    found = {"pilot_module_model_" + match.group(1).decode("ascii")
             for match in _MODEL_TAG.finditer(data)}
    if found != {expected}:
        raise ValueError(f"module {name} is not built for {model}: expected tag "
                         f"{expected}, found {', '.join(sorted(found)) or 'none'}")
    for other in known:
        if other != model and re.match(rf"^{re.escape(other)}[_-]", name, re.IGNORECASE):
            raise ValueError(f"module filename {name} is tagged for {other}, not {model}")
    return expected


def _validate_elf(module: Path, expected_sha256: str, model: str | None = None) -> tuple[str, str | None]:
    if module.suffix != ".so" or not _module_name(module.name):
        raise ValueError("module filename must be a valid .so name")
    _no_symlink(module)
    if not module.is_file():
        raise ValueError("module must be a regular file")
    if not 64 <= module.stat().st_size <= 2 * 1024 * 1024:
        raise ValueError("module size must be 64 bytes..2 MiB")
    data = module.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if not re.fullmatch(r"[0-9a-fA-F]{64}", expected_sha256) or digest.lower() != expected_sha256.lower():
        raise ValueError("module SHA-256 does not match expected_sha256")
    header = data[:20]
    if len(header) < 20 or header[:4] != b"\x7fELF" or header[4:6] != b"\x02\x01":
        raise ValueError("module must be an ELF64 file")
    byteorder = "little" if header[5] == 1 else "big"
    if int.from_bytes(header[18:20], byteorder) != 183:
        raise ValueError("module ELF machine must be AArch64")
    return digest, _check_module_model(data, module.name, model)


def _backup_dir(base: Path, card: Path) -> Path:
    if not base.is_absolute():
        raise ValueError("backup_dir must be an explicit absolute path")
    _no_symlink(base, allow_missing=True)
    base_real, card_real = base.resolve(), card.resolve()
    if base_real == card_real or card_real in base_real.parents or base_real in card_real.parents:
        raise ValueError("backup_dir must be separate from the card root")
    base.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    for suffix in range(1000):
        candidate = base / (stamp if suffix == 0 else f"{stamp}-{suffix}")
        try:
            candidate.mkdir()
            return candidate
        except FileExistsError:
            continue
    raise FileExistsError("could not allocate a unique backup directory")


def deploy_module(module: Path, card: Path, expected_sha256: str,
                  backup_dir: Path, model: str | None = None) -> dict[str, Any]:
    """Validate, back up and install one public module on an explicitly named card root.

    Pass ``model`` to enforce the body identity: the module must export exactly
    that model's tag and must not be named after a different registered body.
    """
    module, card, backup_dir = Path(module), Path(card), Path(backup_dir)
    digest, model_tag = _validate_elf(module, expected_sha256, model)
    if not card.is_absolute():
        raise ValueError("card root must be an explicit absolute path")
    _no_symlink(card)
    pilot = card / "pilot"
    _no_symlink(pilot, allow_missing=True)
    if pilot.exists() and not pilot.is_dir():
        raise ValueError("/pilot must be a directory")
    manifest = _manifest_file(card, optional=True)
    target = _safe_card_file(card, f"pilot/{module.name}", optional=True)
    names = _read_manifest(manifest)
    merged = list(dict.fromkeys(names))
    if module.name not in merged:
        merged.append(module.name)
    if len(merged) > 16:
        raise ValueError("manifest would exceed 16 modules")

    # All source and current-card inputs are validated before allocating backups
    # or changing card contents.
    backup = _backup_dir(backup_dir, card)
    # Write the Fsys-safe name and, when a legacy manifest is already on the
    # card, mirror the payload into it so both stay consistent.
    manifests = [_safe_card_file(card, f"pilot/{_MANIFEST}", optional=True)]
    legacy = _safe_card_file(card, f"pilot/{_LEGACY_MANIFESTS[0]}", optional=True)
    if legacy.exists():
        manifests.append(legacy)
    for path in manifests:
        if path.exists():
            shutil.copy2(path, backup / path.name)
    if target.exists():
        shutil.copy2(target, backup / module.name)

    pilot.mkdir(parents=True, exist_ok=True)
    temp_module = pilot / f".{module.name}.{os.getpid()}.tmp"
    if temp_module.exists() or temp_module.is_symlink():
        raise FileExistsError(temp_module)
    try:
        with module.open("rb") as source, temp_module.open("xb") as dest:
            shutil.copyfileobj(source, dest)
            dest.flush()
            os.fsync(dest.fileno())
        if _sha256(temp_module) != digest:
            raise IOError("module readback SHA-256 mismatch")
        os.replace(temp_module, target)
        if _sha256(target) != digest:
            raise IOError("installed module SHA-256 mismatch")

        payload = json.dumps({"modules": merged}, ensure_ascii=False, indent=2) + "\n"
        for destination in manifests:
            fd, temp_name = tempfile.mkstemp(prefix=".modules.", suffix=".tmp", dir=pilot)
            temp_manifest = Path(temp_name)
            try:
                with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as stream:
                    stream.write(payload)
                    stream.flush()
                    os.fsync(stream.fileno())
                os.replace(temp_manifest, destination)
            finally:
                if temp_manifest.exists():
                    temp_manifest.unlink()
    finally:
        if temp_module.exists():
            temp_module.unlink()
    return {"sha256": digest, "backup_dir": str(backup), "modules": merged,
            "model": model, "model_tag": model_tag}


def list_modules(card: Path) -> list[dict[str, str]]:
    """Return manifest modules and hashes for files directly under /pilot."""
    manifest = _manifest_file(Path(card), optional=False)
    names = _read_manifest(manifest)
    result = []
    for name in names:
        path = _safe_card_file(Path(card), f"pilot/{name}")
        result.append({"name": name, "sha256": _sha256(path)})
    return result


def export_logs(card: Path, out: Path) -> dict[str, Any]:
    """Copy only the Pilot global log and manifest-named module logs."""
    card, out = Path(card), Path(out)
    if not out.is_absolute():
        raise ValueError("out must be an explicit absolute path")
    _no_symlink(card)
    _no_symlink(out, allow_missing=True)
    if card.resolve() == out.resolve() or card.resolve() in out.resolve().parents or out.resolve() in card.resolve().parents:
        raise ValueError("log output must be outside the card")
    names = _read_manifest(_manifest_file(card, optional=True))
    sources = [("pilot/log.txt", "log.txt")]
    sources.extend((f"pilot/modules/{Path(name).stem}/module.log", f"modules/{Path(name).stem}/module.log") for name in names)
    out.mkdir(parents=True, exist_ok=True)
    copied = []
    for relative, destination in sources:
        src = _safe_card_file(card, relative, optional=True)
        if not src.exists():
            continue
        dst = out / destination
        _no_symlink(dst, allow_missing=True)
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
        copied.append(destination)
    return {"output_dir": str(out), "files": copied}
