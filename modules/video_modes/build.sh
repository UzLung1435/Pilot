#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
MOD="$ROOT/modules/video_modes"
SYSROOT=${PILOT_A6700_SYSROOT:-/home/cameraemu/.cache/a6700-kernel-20230712/a6700-original-boot/pilot-merged-root-test}
CC=${AARCH64_CC:-aarch64-linux-gnu-gcc-11}
OUT=${1:-$ROOT/build/pilot_video_modes.so}
TMP=$(mktemp -d /tmp/pilot-video-modes.XXXXXX)
trap 'rm -rf "$TMP"' EXIT

COMMON=(--sysroot="$SYSROOT" -std=c11 -O2 -march=armv8-a -mno-outline-atomics -fno-stack-protector -fPIC -fvisibility=hidden -Wall -Wextra -Werror)
LINK=(--sysroot="$SYSROOT" -shared -nostdlib -Wl,--no-undefined -Wl,--no-as-needed -Wl,--build-id=none -L"$SYSROOT/lib")
LIBS=(-l:libpthread.so.0 -l:libdl.so.2 -l:libc.so.6 -lgcc)

"$CC" "${COMMON[@]}" -I "$ROOT/patch" -I "$ROOT/sdk" -I "$ROOT/modules/menu_ui" \
    -c "$MOD/pilot_video_modes.c" -o "$TMP/pilot_video_modes.o"

mkdir -p "$(dirname "$OUT")"
"$CC" "${LINK[@]}" -Wl,-soname,pilot_video_modes.so \
    -o "$OUT" "$TMP/pilot_video_modes.o" "${LIBS[@]}"

ELF_HEADER="$(readelf -h "$OUT")"
ELF_DYNAMIC="$(readelf -d "$OUT")"
ELF_SYMBOLS="$(readelf -sW "$OUT")"
grep -q AArch64 <<<"$ELF_HEADER"
if grep -q '(TEXTREL)' <<<"$ELF_DYNAMIC"; then exit 1; fi
grep -q 'pilot_module_init' <<<"$ELF_SYMBOLS"
sha256sum "$OUT"
stat -c 'size_bytes=%s' "$OUT"
