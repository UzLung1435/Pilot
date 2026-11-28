#!/usr/bin/env bash
# Build the CP side patch blob for ILCE-7CM2 2.01.
#
#   patch.bin   hook + handler, linked at the cave (0x00C63EA4)
#   branch.bin  4 byte b.w that replaces the original prologue at 0x004B3B0A
#
# Both are then spliced into cpapp-b.bin at
#   branch.bin -> file offset 0x003ABB0A (VA 0x004B3B0A)
#   patch.bin  -> file offset 0x00B5BEA4 (VA 0x00C63EA4)
set -euo pipefail

CC=${ARM_CC:-arm-linux-gnueabi-gcc}
OC=${ARM_OBJCOPY:-arm-linux-gnueabi-objcopy}

HERE="$(cd "$(dirname "$0")" && pwd)"
OUT=${1:-"$HERE/patch.bin"}

CFLAGS=(
  -mcpu=cortex-a9 -mthumb -mfloat-abi=soft
  -O2 -ffreestanding -fno-builtin -fno-stack-protector
  -nostdlib -nostartfiles -Wall -Wextra -Werror
)

cd "$HERE"

"$CC" "${CFLAGS[@]}" -Wl,-e,cp_receiver_hook -T link.ld \
    -o patch.elf hook.S patch_handler.c
"$OC" -O binary -j .patch patch.elf "$OUT"

"$CC" "${CFLAGS[@]}" -Wl,-e,branch_site -T branch.ld \
    -o branch.elf hook_branch.S
"$OC" -O binary -j .hb branch.elf branch.bin

echo "== patch code =="
stat -c '%n %s bytes' "$OUT"
echo "== hook branch =="
stat -c '%n %s bytes' branch.bin
od -An -tx1 branch.bin
echo "== cave head =="
od -An -tx1 -N 32 "$OUT"
echo "== symbols =="
"$CC" -mcpu=cortex-a9 -mthumb -mfloat-abi=soft -Wl,-e,cp_receiver_hook -T link.ld \
    -o /dev/null hook.S patch_handler.c >/dev/null 2>&1 || true
arm-linux-gnueabi-nm -n patch.elf 2>/dev/null | head -20 || true
