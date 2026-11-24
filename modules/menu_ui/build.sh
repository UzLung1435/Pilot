#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MODULE_DIR="$ROOT/modules/menu_ui"
BUILD_DIR="$ROOT/build"
SYSROOT="${PILOT_A6700_SYSROOT:-/home/cameraemu/.cache/a6700-kernel-20230712/a6700-original-boot/pilot-merged-root-test}"
CC="${AARCH64_CC:-aarch64-linux-gnu-gcc-11}"
OUT="${1:-$BUILD_DIR/pilot_menu_ui.so}"
# Menu UI hardcodes ILCE-6700 2.00 appFw GOT/menu-table offsets; it is not a
# portable "pure framework" module and must not be built for another body.
MODEL="${PILOT_MODEL:-a6700}"
if [ "$MODEL" != "a6700" ]; then
    echo "Menu UI module is ILCE-6700 2.00 only; refusing PILOT_MODEL=$MODEL" >&2
    exit 2
fi
MODEL_MACRO=PILOT_MODEL_A6700

TMP="$(mktemp -d /tmp/pilot-menu-ui.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$(dirname "$OUT")"
"$CC" -std=c11 -O2 -march=armv8-a -mno-outline-atomics \
    -fno-stack-protector -fPIC -fvisibility=hidden -Wall -Wextra -Werror \
    -D"$MODEL_MACRO" \
    -I "$ROOT/patch" -c "$MODULE_DIR/pilot_menu_ui.c" \
    -o "$TMP/pilot_menu_ui.o"

"$CC" -shared -nostdlib -Wl,-soname,pilot_menu_ui.so \
    -Wl,--no-undefined -Wl,--no-as-needed -Wl,--build-id=none \
    -L"$SYSROOT/lib" -o "$OUT" "$TMP/pilot_menu_ui.o" \
    -l:libpthread.so.0 -l:libdl.so.2 -l:libc.so.6 -lgcc

ELF_HEADER="$(readelf -h "$OUT")"
ELF_DYNAMIC="$(readelf -d "$OUT")"
ELF_SYMBOLS="$(readelf -sW "$OUT")"
grep -q AArch64 <<<"$ELF_HEADER"
grep -q '(NEEDED)' <<<"$ELF_DYNAMIC"
if grep -q '(TEXTREL)' <<<"$ELF_DYNAMIC"; then
    echo "text relocation found in pilot_menu_ui.so" >&2
    exit 1
fi
grep -q 'pilot_module_init' <<<"$ELF_SYMBOLS"
grep -q 'pilot_module_model_a6700' <<<"$ELF_SYMBOLS"
sha256sum "$OUT"
stat -c '%n %s bytes' "$OUT"
