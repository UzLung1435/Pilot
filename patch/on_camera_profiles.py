"""Accepted appFw source profiles for one camera body.

A patch map records one canonical image hash, but a body may legitimately run a
known in-place modification of the *same* firmware image.  The ILCE-7CM2 2.01
case is the FL2/FL3 Creative Look port (`oldProjects/a7c2-fl23-research`),
profile ``fl23``: eight in-place writes of identical length that never touch the
Pilot hook site, the injected island, the ELF program header fields or any GOT
slot the Pilot patch rewrites.

Each accepted source carries both the hash of the unpatched image and the hash
of the same image after the Pilot windows are applied, so the installer keeps
telling "factory/modified image" (``baseline``) apart from "Pilot already
installed" (``patched``) on every supported source.

The map keeps ``target.binary_sha256`` / ``target.patched_binary_sha256`` as the
canonical stock description; ``target.accepted_sources`` adds the variants.
"""

from __future__ import annotations

import hashlib

STOCK_PROFILE = "stock"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def source_profiles(patch_map: dict) -> list[dict]:
    """Return every accepted source, canonical stock entry first."""
    target = patch_map.get("target") or {}
    rows: list[dict] = []
    canonical = target.get("binary_sha256")
    for entry in target.get("accepted_sources") or []:
        rows.append({
            "profile": entry.get("profile") or "unnamed",
            "binary_sha256": entry["binary_sha256"],
            "patched_binary_sha256": entry.get("patched_binary_sha256"),
            "note": entry.get("note", ""),
        })
    if canonical and all(row["profile"] != STOCK_PROFILE for row in rows):
        rows.insert(0, {
            "profile": STOCK_PROFILE,
            "binary_sha256": canonical,
            "patched_binary_sha256": target.get("patched_binary_sha256"),
            "note": "canonical stock image",
        })
    return rows


def classify_digest(digest: str, size: int, patch_map: dict) -> tuple[str, str | None]:
    """Classify a precomputed image digest without loading the whole image."""
    target = patch_map.get("target") or {}
    expected_size = target.get("binary_size")
    if expected_size is not None and size != expected_size:
        return "unknown", None
    for row in source_profiles(patch_map):
        if digest == row["binary_sha256"]:
            return "baseline", row["profile"]
        if row["patched_binary_sha256"] and digest == row["patched_binary_sha256"]:
            return "patched", row["profile"]
    return "unknown", None


def classify(data: bytes, patch_map: dict) -> tuple[str, str | None]:
    """Return ``(state, profile)`` with state in baseline/patched/unknown."""
    return classify_digest(sha256(data), len(data), patch_map)


def validate(patch_map: dict) -> None:
    """Reject malformed or contradictory source descriptions early."""
    seen: dict[str, str] = {}
    for row in source_profiles(patch_map):
        for key in ("binary_sha256", "patched_binary_sha256"):
            digest = row.get(key)
            if digest is None:
                continue
            if not isinstance(digest, str) or len(digest) != 64 or \
                    any(c not in "0123456789abcdef" for c in digest):
                raise ValueError(f"profile {row['profile']}: {key} is not a SHA-256")
            owner = seen.get(digest)
            if owner is not None and owner != row["profile"]:
                raise ValueError(f"profiles {owner} and {row['profile']} share a hash")
            seen[digest] = row["profile"]


def describe(patch_map: dict) -> str:
    """One-line summary for logs and error messages."""
    parts = []
    for row in source_profiles(patch_map):
        parts.append(f"{row['profile']}={row['binary_sha256'][:12]}")
    return ", ".join(parts)
