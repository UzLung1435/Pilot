#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
cc=${PILOT_CC:-aarch64-linux-gnu-gcc-11}
out=${PILOT_FLASH_OUT:-$root/patch/pilot_ap_flash.so}

"$cc" -std=c11 -Os -march=armv8-a -fPIC -fvisibility=hidden \
    -fno-stack-protector -fno-builtin -Wall -Wextra -Werror \
    -shared -nostdlib -Wl,-init,_init -Wl,--no-undefined -Wl,--build-id=none \
    -o "$out" "$root/patch/pilot_ap_flash.c"
readelf -h "$out" | grep -q AArch64
readelf -d "$out" | grep -q '(INIT)'
if readelf -d "$out" | grep -q '(NEEDED)'; then
    echo "unexpected dynamic dependency: $out" >&2
    exit 1
fi
sha256sum "$out"
