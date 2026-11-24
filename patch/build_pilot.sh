#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PATCH_DIR="$ROOT/patch"
BUILD_DIR="$ROOT/build"
SYSROOT="${PILOT_SYSROOT:?set PILOT_SYSROOT to a target sysroot}"
CC="${AARCH64_CC:-aarch64-linux-gnu-gcc-11}"
OUT="${1:-$BUILD_DIR/pilot_modular_candidate.so}"

mkdir -p "$(dirname "$OUT")"
"$CC" -std=c11 -O2 -march=armv8-a -mno-outline-atomics \
    -fno-stack-protector -fPIC -fvisibility=hidden -Wall -Wextra -Werror \
    -shared -nostdlib -Wl,-soname,pilot.so -Wl,--no-undefined \
    -Wl,--no-as-needed -Wl,--build-id=none -L"$SYSROOT/lib" \
    -I "$PATCH_DIR" -o "$OUT" \
    "$PATCH_DIR/pilot_entry.c" \
    "$PATCH_DIR/pilot_fsys.c" \
    "$PATCH_DIR/pilot_time.c" \
    "$PATCH_DIR/pilot_api.c" \
    "$PATCH_DIR/pilot_memory.c" \
    "$PATCH_DIR/pilot_cp_mem.c" \
    "$PATCH_DIR/pilot_module_dir.c" \
    "$PATCH_DIR/pilot_module_files.c" \
    "$PATCH_DIR/pilot_modules.c" \
    -l:libpthread.so.0 -l:libdl.so.2 -l:libc.so.6 -lgcc

ELF_HEADER="$(readelf -h "$OUT")"
ELF_DYNAMIC="$(readelf -d "$OUT")"
ELF_SYMBOLS="$(readelf -sW "$OUT")"
grep -q AArch64 <<<"$ELF_HEADER"
grep -q '(NEEDED)' <<<"$ELF_DYNAMIC"
if grep -q '(TEXTREL)' <<<"$ELF_DYNAMIC"; then
    echo "text relocation found in Pilot DSO" >&2
    exit 1
fi
# pilot_a6700_* are the historical cross-model ABI names; the A6700 and A7C II
# builds both export them so the same island string table works on both bodies.
for symbol in pilot_a6700_v1_on_mount pilot_a6700_v1_marker pilot_a6700_get_api; do
    grep -q "$symbol" <<<"$ELF_SYMBOLS"
done
sha256sum "$OUT"
stat -c '%n %s bytes' "$OUT"
