#!/usr/bin/env bash
# Build pilot_canvas.so for the A6700.
#
#   PILOT_SYSROOT=/path/to/a6700-sysroot bash modules/pilot_canvas/build.sh
#   PILOT_MODEL=a6700 PILOT_A6700_SYSROOT=... AARCH64_CC=aarch64-linux-gnu-gcc-11 \
#       bash modules/pilot_canvas/build.sh [output.so]
#
# PILOT_SYSROOT/PILOT_CC follow patch/build_pilot_dso.sh, while
# PILOT_A6700_SYSROOT/AARCH64_CC keep the names modules/menu_ui/build.sh uses.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MODULE_DIR="$ROOT/modules/pilot_canvas"
BUILD_DIR="$ROOT/build"
MODEL="${PILOT_MODEL:-a6700}"

# canvas_core.c, canvas_text.c, canvas_panel.c and canvas_cp_sensors.c all
# reach hard-coded appFw.so GOT slots, DISP mode BSS offsets and CP RAM
# addresses measured on ILCE-6700 firmware 2.00.  On another body those
# addresses point at unrelated memory, so the module must not be built for one.
if [ "$MODEL" != "a6700" ]; then
    cat >&2 <<EOF
pilot_canvas targets the A6700 only: PILOT_MODEL=$MODEL is not supported.
The module reaches A6700 2.00 native addresses (appFw.so GOT slots for the
frame/touch/DISP hooks, DISP mode BSS, CP RAM OPD table) that have not been
located on any other body.
EOF
    exit 1
fi

DEFAULT_SYSROOT=/home/cameraemu/.cache/a6700-kernel-20230712/a6700-original-boot/pilot-merged-root-test
# A sysroot unpacked out of the firmware next to this checkout, used by the
# patch tooling; preferred over the historical absolute path when present.
LOCAL_SYSROOT="$ROOT/../work/pilot-a7c2/sysroot"
SYSROOT="${PILOT_SYSROOT:-${PILOT_A6700_SYSROOT:-}}"
if [ -z "$SYSROOT" ]; then
    if [ -d "$LOCAL_SYSROOT/usr/include" ] || [ -d "$LOCAL_SYSROOT/lib" ]; then
        SYSROOT="$LOCAL_SYSROOT"
    else
        SYSROOT="$DEFAULT_SYSROOT"
    fi
fi
if [ -n "${PILOT_CC:-}" ]; then
    CC="$PILOT_CC"
elif [ -n "${AARCH64_CC:-}" ]; then
    CC="$AARCH64_CC"
elif command -v aarch64-linux-gnu-gcc-11 >/dev/null 2>&1; then
    CC=aarch64-linux-gnu-gcc-11
else
    CC=aarch64-linux-gnu-gcc
fi
OUT="${1:-$BUILD_DIR/pilot_canvas.so}"
TMP="$(mktemp -d /tmp/pilot-canvas.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

# --sysroot does not do what this build needs.  A Debian/Ubuntu cross gcc puts
# its own $prefix/aarch64-linux-gnu/{include,lib} ahead of the sysroot, so the
# desktop glibc 2.38 headers (__isoc23_*) and stub libraries would still win
# and the resulting DSO dies on the camera with `GLIBC_2.34' not found.  Point
# -I and -L at the sysroot explicitly instead; the trailing glibc guard below
# fails the build if either one is missing.
SYS_INC=""
[ -d "$SYSROOT/usr/include" ] && SYS_INC="$SYSROOT/usr/include"
SYS_LIB=""
if [ -d "$SYSROOT/lib" ]; then
    SYS_LIB="$SYSROOT/lib"
elif [ -d "$SYSROOT/usr/lib" ]; then
    SYS_LIB="$SYSROOT/usr/lib"
fi

echo "sysroot:   $SYSROOT"
echo "headers:   ${SYS_INC:-<host compiler defaults>}"
echo "libraries: ${SYS_LIB:-<host compiler defaults>}"

SRCS=(
    pilot_canvas.c
    canvas_core.c
    canvas_text.c
    canvas_panel.c
    canvas_cp_sensors.c
    canvas_scope_widget.c
)

mkdir -p "$(dirname "$OUT")"
INC_FLAGS=()
if [ -n "$SYS_INC" ]; then INC_FLAGS=(-I "$SYS_INC"); fi
LIB_FLAGS=()
if [ -n "$SYS_LIB" ]; then LIB_FLAGS=(-L "$SYS_LIB"); fi
OBJS=()
for src in "${SRCS[@]}"; do
    obj="$TMP/${src%.c}.o"
    "$CC" -std=c11 -O2 -march=armv8-a -mno-outline-atomics \
        -fno-stack-protector -fPIC -fvisibility=hidden -Wall -Wextra -Werror \
        -DPILOT_MODEL_A6700 \
        "${INC_FLAGS[@]}" \
        -I "$ROOT/patch" -I "$ROOT/modules/menu_ui" -I "$MODULE_DIR" \
        -c "$MODULE_DIR/$src" -o "$obj"
    OBJS+=("$obj")
done

"$CC" -shared -nostdlib -Wl,-soname,pilot_canvas.so \
    -Wl,--no-undefined -Wl,--no-as-needed -Wl,--build-id=none \
    "${LIB_FLAGS[@]}" -o "$OUT" "${OBJS[@]}" \
    -l:libpthread.so.0 -l:libdl.so.2 -l:libc.so.6 -lgcc

# The loader validates the body tag before pilot_module_init() runs, so the
# tag has to be present in the dynamic symbol table, not just in .symtab.
ELF_HEADER="$(readelf -h "$OUT")"
ELF_DYNAMIC="$(readelf -d "$OUT")"
ELF_DYNSYMS="$(readelf --dyn-syms -W "$OUT")"
grep -q AArch64 <<<"$ELF_HEADER"
grep -q '(NEEDED)' <<<"$ELF_DYNAMIC"
if grep -q '(TEXTREL)' <<<"$ELF_DYNAMIC"; then
    echo "text relocation found in $OUT" >&2
    exit 1
fi
for symbol in pilot_module_init pilot_module_model_a6700 pilot_canvas_probe_v1; do
    grep -qF "$symbol" <<<"$ELF_DYNSYMS"
done

# The camera runs glibc 2.25, so a DSO linked against a desktop cross sysroot
# loads fine on the build host and then dies on the camera with
# "version `GLIBC_2.34' not found".  Reject that here instead: no __isoc23_*
# redirections (glibc >= 2.38 headers) and no GLIBC_x.y above 2.25.
if grep -q '__isoc23_' <<<"$ELF_DYNSYMS"; then
    echo "$OUT references __isoc23_* symbols: built against glibc 2.38+ headers," >&2
    echo "not the camera sysroot. Set PILOT_SYSROOT to an A6700 sysroot." >&2
    exit 1
fi
MAX_GLIBC="$(readelf -VW "$OUT" | grep -oE 'GLIBC_[0-9]+\.[0-9]+' | sed 's/^GLIBC_//' | sort -uV | tail -1)"
if [ -n "$MAX_GLIBC" ]; then
    GLIBC_MAJOR="${MAX_GLIBC%%.*}"
    GLIBC_MINOR="${MAX_GLIBC#*.}"
    if [ "$GLIBC_MAJOR" -gt 2 ] ||
       { [ "$GLIBC_MAJOR" -eq 2 ] && [ "$GLIBC_MINOR" -gt 25 ]; }; then
        echo "$OUT requires GLIBC_$MAX_GLIBC but the camera provides glibc 2.25." >&2
        echo "Set PILOT_SYSROOT to an A6700 sysroot, not a desktop cross sysroot." >&2
        exit 1
    fi
fi
echo "glibc requirement: <= GLIBC_${MAX_GLIBC:-none}"
sha256sum "$OUT"
stat -c '%n %s bytes' "$OUT"
