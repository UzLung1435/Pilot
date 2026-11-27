#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MODULE_DIR="$ROOT/modules/menu_ui"
BUILD_DIR="$ROOT/build"
# Auto-detect model from parent directory or use environment variable
MODEL="${PILOT_MODEL:-a7c2}"
if [ "$MODEL" = "a6700" ]; then
    SYSROOT="${PILOT_A6700_SYSROOT:-/home/cameraemu/.cache/a6700-kernel-20230712/a6700-original-boot/pilot-merged-root-test}"
elif [ "$MODEL" = "a7c2" ]; then
    SYSROOT="${PILOT_A7C2_SYSROOT:-/opt/a7c2-sysroot}"
else
    echo "Unknown model: $MODEL" >&2
    exit 1
fi
CC="${AARCH64_CC:-aarch64-linux-gnu-gcc-11}"
OUT="${1:-$BUILD_DIR/pilot_menu_ui.so}"

TMP="$(mktemp -d /tmp/pilot-menu-ui.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$(dirname "$OUT")"

# Set model-specific compile flags
if [ "$MODEL" = "a7c2" ]; then
    MODEL_FLAGS="-DPILOT_MODEL_A7C2"
else
    MODEL_FLAGS=""
fi

"$CC" -std=c11 -O2 -march=armv8-a -mno-outline-atomics \
    -fno-stack-protector -fPIC -fvisibility=hidden -Wall -Wextra -Werror \
    $MODEL_FLAGS -I "$ROOT/patch" -c "$MODULE_DIR/pilot_menu_ui.c" \
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
sha256sum "$OUT"
stat -c '%n %s bytes' "$OUT"
