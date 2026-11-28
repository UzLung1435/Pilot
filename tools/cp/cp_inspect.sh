#!/usr/bin/env bash
# cp_inspect.sh <file-offset>:<length> ...  -- hexdump regions of the A7C2 CP image
set -uo pipefail
B=/mnt/d/Code/Sony_Cam_Compare/oldProjects/FlashDir/backup1/candidate/cpapp-b.bin
for spec in "$@"; do
  off_hex=${spec%%:*}
  len_hex=${spec##*:}
  off=$((off_hex))
  len=$((len_hex))
  va=$((off + 0x108000))
  printf -- '--- file 0x%x (va 0x%x) ---\n' "$off" "$va"
  xxd -s "$off" -l "$len" "$B"
done
