from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

from .deploy import deploy_module, export_logs, list_modules
from .ram import build_module, generate_module
from .spec import load_spec, patch_image


def parser():
    root = argparse.ArgumentParser(description="Pilot local patch tools; research inputs remain external")
    commands = root.add_subparsers(dest="command", required=True)
    inspect = commands.add_parser("inspect", help="validate an immutable patch description")
    inspect.add_argument("spec", type=Path)
    image = commands.add_parser("image", help="apply/restore a patch to a local image copy")
    image.add_argument("spec", type=Path)
    image.add_argument("input", type=Path)
    image.add_argument("--out", type=Path, required=True)
    image.add_argument("--restore", action="store_true")
    ram = commands.add_parser("ram", help="generate or compile a checked Pilot RAM module")
    ram.add_argument("action", choices=("source", "build"))
    ram.add_argument("spec", type=Path)
    ram.add_argument("--out", required=True, type=Path)
    ram.add_argument("--restore", action="store_true")
    ram.add_argument("--cc", help="explicit AArch64 compiler executable")
    ram.add_argument("--sysroot", type=Path)
    modules = commands.add_parser("modules")
    sub = modules.add_subparsers(dest="action", required=True)
    listing = sub.add_parser("list")
    listing.add_argument("--card", required=True, type=Path)
    deploy = sub.add_parser("deploy")
    deploy.add_argument("module", type=Path)
    deploy.add_argument("--card", required=True, type=Path)
    deploy.add_argument("--sha256", required=True)
    deploy.add_argument("--backup-dir", required=True, type=Path)
    deploy.add_argument("--model", help="required model identity of the module (see models.json)")
    logs = commands.add_parser("logs", help="export Pilot logs from an explicitly selected card")
    logs.add_argument("--card", required=True, type=Path)
    logs.add_argument("--out", required=True, type=Path)
    flash = commands.add_parser("flash", help="call on_camera.py with external local inputs")
    flash.add_argument("action", choices=("check", "install", "restore", "update-pilot", "clean"))
    flash.add_argument("--map", dest="map_path", required=True, type=Path)
    flash.add_argument("--transport-root", required=True, type=Path)
    flash.add_argument("--state-dir", required=True, type=Path)
    flash.add_argument("--writer", type=Path)
    flash.add_argument("--pilot", type=Path)
    flash.add_argument("--yes", action="store_true")
    return root


def main(argv=None):
    args = parser().parse_args(argv)
    try:
        if args.command == "inspect":
            result = load_spec(args.spec)
        elif args.command == "image":
            output = patch_image(load_spec(args.spec), args.input.read_bytes(), args.restore)
            with args.out.open("xb") as stream:
                stream.write(output)
            result = {"output": str(args.out), "size_bytes": len(output)}
        elif args.command == "ram":
            spec = load_spec(args.spec)
            if args.action == "build":
                if not args.cc or not args.sysroot:
                    raise ValueError("ram build requires --cc and --sysroot")
                result = build_module(spec, args.out, args.cc, args.sysroot, args.restore)
            else:
                source = generate_module(spec, args.restore)
                with args.out.open("x", encoding="utf-8", newline="\n") as stream:
                    stream.write(source)
                result = {"source": str(args.out)}
        elif args.command == "modules":
            if args.action == "list":
                result = list_modules(args.card)
            else:
                result = deploy_module(args.module, args.card, args.sha256,
                                       args.backup_dir, args.model)
        elif args.command == "logs":
            result = export_logs(args.card, args.out)
        else:
            writing = args.action in ("install", "restore", "update-pilot")
            if writing and not args.yes:
                raise ValueError("flash writes require --yes")
            if args.action != "check" and args.writer is None:
                raise ValueError("this flash action requires --writer")
            if args.action in ("install", "update-pilot") and args.pilot is None:
                raise ValueError("this flash action requires --pilot")
            script = Path(__file__).resolve().parents[2] / "patch/on_camera.py"
            command = [sys.executable, str(script), "--" + args.action, "--map", str(args.map_path),
                       "--transport-root", str(args.transport_root), "--state-dir", str(args.state_dir)]
            for name in ("writer", "pilot"):
                if getattr(args, name) is not None:
                    command.extend(["--" + name, str(getattr(args, name))])
            if args.yes:
                command.append("--yes")
            return subprocess.run(command, check=False).returncode
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2
