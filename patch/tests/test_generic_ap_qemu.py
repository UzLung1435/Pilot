"""Exercise the generic writer with temporary files in QEMU user mode."""
from __future__ import annotations

import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "patch"))
from prepare_ap import WINDOW_IDS, encode_plan


def main() -> None:
    sysroot = os.environ["PILOT_SYSROOT"]
    cc = os.environ.get("PILOT_CC", "aarch64-linux-gnu-gcc-11")
    qemu = os.environ.get("PILOT_QEMU", "qemu-aarch64")
    with tempfile.TemporaryDirectory(prefix="pilot-generic-ap-") as directory:
        tmp = Path(directory)
        app, mode, result = tmp / "appFw.so", tmp / "mode", tmp / "result"
        plan_path, source, target = tmp / "plan", tmp / "source.so", tmp / "pilot.so"
        loader_src, loader, writer = tmp / "loader.c", tmp / "loader", tmp / "writer.so"
        loader_src.write_text('#include <dlfcn.h>\nint main(int argc,char **argv){if(argc!=2)return 2; return dlopen(argv[1],RTLD_NOW|RTLD_GLOBAL)?0:1;}\n', encoding="ascii")
        subprocess.run([cc, "-std=c11", "-O2", "-Wl,--dynamic-linker,/lib/ld-linux-aarch64.so.1", f"-Wl,-rpath-link,{sysroot}/lib", f"-L{sysroot}/lib", "-o", str(loader), str(loader_src), "-l:libdl.so.2", "-l:libc.so.6", "-static-libgcc"], check=True)
        defines = {"PILOT_FLASH_TEST_APPFW_PATH": app, "PILOT_FLASH_MODE_PATH": mode,
                   "PILOT_FLASH_RESULT_PATH": result, "PILOT_FLASH_PLAN_PATH": plan_path,
                   "PILOT_FLASH_PILOT_PATH": source, "PILOT_BOOTSTRAP_PATH": target}
        flags = [f'-D{name}="{path}"' for name, path in defines.items()]
        subprocess.run([cc, "-std=c11", "-Os", "-march=armv8-a", "-fPIC", "-fvisibility=hidden", "-fno-stack-protector", "-fno-builtin", "-Wall", "-Wextra", "-Werror", "-shared", "-nostdlib", "-Wl,-init,_init", "-Wl,--no-undefined", "-Wl,--build-id=none", "-DPILOT_FLASH_TEST", *flags, "-o", str(writer), str(ROOT / "patch/pilot_ap_flash.c")], check=True)

        original = bytearray((i * 17) % 256 for i in range(4096))
        app.write_bytes(original)
        pilot = bytearray(128)
        pilot[:6] = b"\x7fELF\x02\x01"
        pilot[18:20] = (183).to_bytes(2, "little")
        source.write_bytes(pilot)
        offsets = (32, 128, 512, 1024)
        windows = [(name, off, bytes(original[off:off + 4]), bytes([i + 1]) * 4)
                   for i, (name, off) in enumerate(zip(WINDOW_IDS, offsets))]
        plan = encode_plan(windows, len(original))
        plan_path.write_bytes(plan)

        def execute(action: str) -> str:
            mode.write_text(action + "\n", encoding="ascii")
            result.unlink(missing_ok=True)
            subprocess.run([qemu, "-L", sysroot, str(loader), str(writer)], check=True)
            return result.read_text(encoding="ascii")

        assert "state=baseline" in execute("check")
        assert "RESULT=OK" in execute("write")
        expected = bytearray(original)
        for _, off, _, after in windows:
            expected[off:off + len(after)] = after
        assert app.read_bytes() == expected and target.read_bytes() == pilot
        assert "state=patched" in execute("check")
        assert "RESULT=OK" in execute("restore")
        assert app.read_bytes() == original

        altered = bytearray(original)
        altered[offsets[0]] ^= 1
        app.write_bytes(altered)
        assert "reason=patch_windows_unknown" in execute("write")
        assert app.read_bytes() == altered
        app.write_bytes(original)
        plan_path.write_bytes(b"bad")
        assert "reason=invalid_plan" in execute("write")
        assert app.read_bytes() == original
        overlapping = bytearray(plan)
        struct.pack_into("<Q", overlapping, 24 + 16, offsets[0])
        plan_path.write_bytes(overlapping)
        assert "reason=invalid_plan" in execute("write")
        assert app.read_bytes() == original

        plan_path.write_bytes(plan)
        source.unlink()
        assert "reason=bootstrap_install" in execute("write")
        assert app.read_bytes() == original
        updated = bytearray(pilot)
        updated[-1] = 42
        source.write_bytes(updated)
        assert "RESULT=OK" in execute("pilot")
        assert target.read_bytes() == updated and app.read_bytes() == original
        print("PILOT_GENERIC_AP_QEMU_OK")


if __name__ == "__main__":
    main()
