#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PATCH_DIR="$ROOT/patch"
BUILD_DIR="$ROOT/build"
SYSROOT="${PILOT_SYSROOT:?set PILOT_SYSROOT to a target sysroot}"
CC="${AARCH64_CC:-aarch64-linux-gnu-gcc-11}"
PYTHON="${PILOT_PYTHON:-python3}"
MODEL="${PILOT_MODEL:-a6700}"
MODELS="$ROOT/tools/pilot_tool/models.py"
OUT="${1:-$BUILD_DIR/pilot_modular_candidate.so}"

# models.json is the single source of truth; --check also verifies that
# patch/pilot_model.h has not drifted from the registry.
"$PYTHON" "$MODELS" --check >/dev/null
model_value() { "$PYTHON" "$MODELS" "$MODEL" "$1"; }
BUILD_MACRO="$(model_value build_macro)"
SONAME="$(model_value dso_soname)"
MARKER_SYMBOL="$(model_value marker_symbol)"
ON_MOUNT_SYMBOL="$(model_value mount_callback_symbol)"
GET_API_SYMBOL="$(model_value get_api_symbol)"

mkdir -p "$(dirname "$OUT")"
"$CC" -std=c11 -O2 -march=armv8-a -mno-outline-atomics \
    -fno-stack-protector -fPIC -fvisibility=hidden -Wall -Wextra -Werror \
    -D"$BUILD_MACRO" \
    -shared -nostdlib -Wl,-soname,"$SONAME" -Wl,--no-undefined \
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
# Bootstrap exports are model-specific: require this body's three symbols and
# reject another body's marker, so a wrong-model DSO cannot pass a build check.
for symbol in "$ON_MOUNT_SYMBOL" "$MARKER_SYMBOL" "$GET_API_SYMBOL"; do
    grep -qF "$symbol" <<<"$ELF_SYMBOLS"
done
for other in $("$PYTHON" "$MODELS" --ids); do
    if [ "$other" = "$MODEL" ]; then continue; fi
    if grep -qF "pilot_${other}_v1_marker" <<<"$ELF_SYMBOLS"; then
        echo "Pilot DSO built for $MODEL exports a marker belonging to $other" >&2
        exit 1
    fi
done
echo "Pilot model=$MODEL soname=$SONAME"
sha256sum "$OUT"
stat -c '%n %s bytes' "$OUT"
