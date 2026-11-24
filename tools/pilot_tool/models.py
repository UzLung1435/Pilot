"""Single source of truth for Pilot's per-model identities.

Both the C build (``patch/pilot_model.h``), the island generator, the flashing
entry point and the module deployment checks derive their model-specific names
from ``models.json``.  ``verify_c_header()`` keeps the C header in sync; call it
from builds and tests so a renamed symbol cannot silently diverge.

CLI (used by shell build scripts)::

    python tools/pilot_tool/models.py a7c2 dso_soname
    python tools/pilot_tool/models.py --check
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REGISTRY = ROOT / "models.json"
C_HEADER = ROOT / "patch" / "pilot_model.h"

_MODEL_ID = re.compile(r"^[a-z][a-z0-9_]{0,15}$")
_SYMBOL = re.compile(r"^[A-Za-z_][A-Za-z0-9_]{0,63}$")
_PATH = re.compile(r"^/[A-Za-z0-9_./-]{1,95}$")
_KEYS = (
    "camera", "firmware", "build_macro", "dso_soname", "dso_path",
    "boot_trace_path", "marker_symbol", "mount_callback_symbol",
    "get_api_symbol", "module_model_symbol", "flash_header",
    "island_strings", "note",
)


def load_models(path: Path | None = None) -> dict:
    data = json.loads((path or REGISTRY).read_text(encoding="utf-8-sig"))
    if data.get("schema") != "pilot.models.v1" or not isinstance(data.get("models"), dict):
        raise ValueError("expected a pilot.models.v1 registry")
    if data.get("default") not in data["models"]:
        raise ValueError("default model is not registered")
    slots = data.get("island_string_slots")
    if not isinstance(slots, dict) or not slots:
        raise ValueError("island_string_slots must describe the island layout")
    for model_id, config in data["models"].items():
        if not _MODEL_ID.fullmatch(model_id):
            raise ValueError(f"invalid model id: {model_id}")
        if set(config) != set(_KEYS):
            raise ValueError(f"model {model_id}: expected fields {sorted(_KEYS)}")
        for key in ("build_macro", "marker_symbol", "mount_callback_symbol",
                    "get_api_symbol", "module_model_symbol"):
            if not _SYMBOL.fullmatch(config[key]):
                raise ValueError(f"model {model_id}: invalid {key}")
        for key in ("dso_path", "boot_trace_path"):
            if not _PATH.fullmatch(config[key]):
                raise ValueError(f"model {model_id}: invalid {key}")
        if not re.fullmatch(r"[A-Za-z0-9_.-]{1,64}\.so", config["dso_soname"]):
            raise ValueError(f"model {model_id}: invalid dso_soname")
        header = config["flash_header"]
        if not isinstance(header, str) or not header.endswith("\n") or len(header) > 32 or \
                any(ord(ch) < 32 and ch != "\n" or ord(ch) > 126 for ch in header):
            raise ValueError(f"model {model_id}: invalid flash_header")
        strings = config["island_strings"]
        if set(strings) != set(slots):
            raise ValueError(f"model {model_id}: island_strings must cover every slot")
        for slot, text in strings.items():
            if not isinstance(text, str) or not text or "\0" in text or \
                    len(text.encode("ascii")) + 1 > slots[slot]:
                raise ValueError(f"model {model_id}: island string at {slot} exceeds its slot")
            if any(ord(ch) < 32 or ord(ch) > 126 for ch in text):
                raise ValueError(f"model {model_id}: island string at {slot} is not printable ASCII")
    return data


def model_ids(path: Path | None = None) -> list[str]:
    return sorted(load_models(path)["models"])


def model_config(model_id: str, path: Path | None = None) -> dict:
    data = load_models(path)
    if model_id not in data["models"]:
        raise ValueError(f"unknown Pilot model {model_id!r}; registered: {', '.join(sorted(data['models']))}")
    return data["models"][model_id]


def module_model_symbol(model_id: str, path: Path | None = None) -> str:
    return model_config(model_id, path)["module_model_symbol"]


def runtime_block(model_id: str, path: Path | None = None) -> dict:
    """Fields copied into a patch map so on_camera.py needs no registry file."""
    data = load_models(path)
    if model_id not in data["models"]:
        raise ValueError(f"unknown Pilot model {model_id!r}")
    config = data["models"][model_id]
    block = {"model_id": model_id, **{k: v for k, v in config.items()
                                      if k not in ("note", "island_strings", "build_macro")}}
    # The flash utility must be able to reject a wrong-body DSO without
    # reading this registry, so embed the foreign identities in the map.
    block["foreign_model_ids"] = [k for k in data["models"] if k != model_id]
    block["foreign_marker_symbols"] = [
        other["marker_symbol"] for key, other in data["models"].items()
        if key != model_id]
    return block


def verify_c_header(root: Path | None = None, path: Path | None = None) -> dict:
    """Check that patch/pilot_model.h exposes every registry value and symbol."""
    root = root or ROOT
    header_text = (root / "patch" / "pilot_model.h").read_text(encoding="utf-8")
    # Each macro is defined once per model branch; collect every value so both
    # branches can be checked instead of only the last one.
    defines: dict[str, set[str]] = {}
    for name, value in re.findall(r'^\s*#define\s+([A-Z][A-Z0-9_]*)\s+"([^"\n]*)"\s*$',
                                  header_text, re.MULTILINE):
        defines.setdefault(name, set()).add(value)
    words: dict[str, set[str]] = {}
    for name, value in re.findall(r'^\s*#define\s+([A-Z][A-Z0-9_]*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*$',
                                  header_text, re.MULTILINE):
        words.setdefault(name, set()).add(value)
    data = load_models(path)
    checked = 0
    for model_id in data["models"]:
        if f'"{model_id}"' not in header_text:
            raise ValueError(f"pilot_model.h: PILOT_MODEL_IDS is missing {model_id}")
    for model_id, config in data["models"].items():
        if config["build_macro"] not in header_text:
            raise ValueError(f"pilot_model.h is missing {config['build_macro']}")
        pairs = {
            "PILOT_MODEL_ID": model_id,
            "PILOT_MODEL_NAME": config["camera"],
            "PILOT_DSO_SONAME": config["dso_soname"],
            "PILOT_DSO_PATH": config["dso_path"],
            "PILOT_BOOT_TRACE_PATH": config["boot_trace_path"],
            "PILOT_MARKER_SYMBOL": config["marker_symbol"],
            "PILOT_ON_MOUNT_SYMBOL": config["mount_callback_symbol"],
            "PILOT_GET_API_SYMBOL": config["get_api_symbol"],
            "PILOT_MODULE_MODEL_SYMBOL": config["module_model_symbol"],
        }
        for macro, value in pairs.items():
            if value not in defines.get(macro, set()):
                raise ValueError(f"pilot_model.h: {macro} must define {value!r} for {model_id}")
            checked += 1
        fns = {
            "PILOT_MARKER_FN": config["marker_symbol"],
            "PILOT_ON_MOUNT_FN": config["mount_callback_symbol"],
            "PILOT_GET_API_FN": config["get_api_symbol"],
            "PILOT_MODULE_MODEL_FN": config["module_model_symbol"],
        }
        for macro, value in fns.items():
            if value not in words.get(macro, set()):
                raise ValueError(f"pilot_model.h: {macro} must define {value} for {model_id}")
            checked += 1
    return {"models": sorted(data["models"]), "checked_definitions": checked}


def main(argv: list[str] | None = None) -> int:
    args = list(sys.argv[1:] if argv is None else argv)
    try:
        if args == ["--check"]:
            print(json.dumps(verify_c_header(), ensure_ascii=False))
            return 0
        if args == ["--ids"]:
            sys.stdout.write(" ".join(model_ids()))
            return 0
        if len(args) == 2:
            config = model_config(args[0])
            key = args[1]
            if key not in config or key in ("island_strings", "note"):
                raise ValueError(f"unknown scalar registry key: {key}")
            sys.stdout.write(str(config[key]))
            return 0
        if len(args) == 3 and args[1] == "island_string":
            sys.stdout.write(model_config(args[0])["island_strings"][args[2]])
            return 0
        raise ValueError("usage: models.py <model> <key> | <model> island_string <slot> | --check")
    except (OSError, ValueError, KeyError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
