#!/usr/bin/env bash
set -euo pipefail

# Build Pilot's menu_ui module.
#
#   PILOT_MODEL=a6700  -> pilot_menu_ui.c      (A6700 2.00, layout-array backend)
#   PILOT_MODEL=a7c2   -> pilot_menu_ui_a7c2.c (A7C II 2.01, menu-table + setString backend)
#
# Both variants need a camera sysroot: the host glibc is far newer than the
# camera's glibc 2.25, and the camera fpmath/stdio symbols would otherwise
# leak into the DSO.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MODULE_DIR="$ROOT/modules/menu_ui"
BUILD_DIR="$ROOT/build"
MODEL="${PILOT_MODEL:-a7c2}"
if [ "$#" -ge 1 ]; then
    OUT="$1"
else
    OUT="$BUILD_DIR/pilot_menu_ui.so"
fi

case "$MODEL" in
    a6700)
        SOURCE="pilot_menu_ui.c"
        MODEL_FLAGS=""
        DEFAULT_SYSROOT="${PILOT_A6700_SYSROOT:-/home/cameraemu/.cache/a6700-kernel-20230712/a6700-original-boot/pilot-merged-root-test}"
        ;;
    a7c2)
        SOURCE="pilot_menu_ui_a7c2.c"
        MODEL_FLAGS="-DPILOT_MODEL_A7C2"
        DEFAULT_SYSROOT="/opt/a7c2-sysroot"
        ;;
    *)
        echo "Unknown model: $MODEL (expected a6700 or a7c2)" >&2
        exit 1
        ;;
esac

# --- sysroot discovery ------------------------------------------------------
SYSROOT=""
if [ -n "${PILOT_SYSROOT:-}" ]; then
    SYSROOT="$PILOT_SYSROOT"
elif [ -n "${PILOT_A7C2_SYSROOT:-}" ] && [ "$MODEL" = "a7c2" ]; then
    SYSROOT="$PILOT_A7C2_SYSROOT"
elif [ -d "$ROOT/../work/pilot-a7c2/sysroot/usr/include" ]; then
    SYSROOT="$ROOT/../work/pilot-a7c2/sysroot"
elif [ -d "$DEFAULT_SYSROOT" ]; then
    SYSROOT="$DEFAULT_SYSROOT"
else
    SYSROOT="$DEFAULT_SYSROOT"
fi

# --- compiler discovery -----------------------------------------------------
if [ -n "${PILOT_CC:-}" ]; then
    CC="$PILOT_CC"
elif [ -n "${AARCH64_CC:-}" ]; then
    CC="$AARCH64_CC"
elif command -v aarch64-linux-gnu-gcc-11 >/dev/null 2>&1; then
    CC="aarch64-linux-gnu-gcc-11"
else
    CC="aarch64-linux-gnu-gcc"
fi

# The Debian cross compiler puts its own $prefix/aarch64-linux-gnu/{include,lib}
# ahead of --sysroot, so --sysroot alone still picks the host glibc 2.38
# headers.  Point the include/library search paths at the camera sysroot
# explicitly instead.
INC_FLAGS=()
LIB_FLAGS=()
if [ -d "$SYSROOT/usr/include" ]; then
    INC_FLAGS+=(-I "$SYSROOT/usr/include")
fi
if [ -d "$SYSROOT/lib" ]; then
    LIB_FLAGS+=(-L "$SYSROOT/lib")
fi
if [ -d "$SYSROOT/usr/lib" ]; then
    LIB_FLAGS+=(-L "$SYSROOT/usr/lib")
fi

echo "model:    $MODEL"
echo "source:   $MODULE_DIR/$SOURCE"
echo "compiler: $CC"
echo "sysroot:  $SYSROOT"
if [ ${#INC_FLAGS[@]} -eq 0 ]; then
    echo "warning: $SYSROOT/usr/include is missing, the build will use the host headers" >&2
fi

TMP="$(mktemp -d /tmp/pilot-menu-ui.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$(dirname "$OUT")"

# shellcheck disable=SC2086
"$CC" -std=c11 -O2 -march=armv8-a -mno-outline-atomics \
    -fno-stack-protector -fPIC -fvisibility=hidden -Wall -Wextra -Werror \
    $MODEL_FLAGS "${INC_FLAGS[@]}" \
    -I "$ROOT/patch" -I "$MODULE_DIR" \
    -c "$MODULE_DIR/$SOURCE" -o "$TMP/menu_ui.o"

"$CC" -shared -nostdlib -Wl,-soname,pilot_menu_ui.so \
    -Wl,--no-undefined -Wl,--no-as-needed -Wl,--build-id=none \
    "${LIB_FLAGS[@]}" -o "$OUT" "$TMP/menu_ui.o" \
    -l:libpthread.so.0 -l:libdl.so.2 -l:libc.so.6 -lgcc

# --- sanity checks ----------------------------------------------------------
ELF_HEADER="$(readelf -h "$OUT")"
ELF_DYNAMIC="$(readelf -d "$OUT")"
ELF_SYMBOLS="$(readelf -sW "$OUT")"

grep -q AArch64 <<<"$ELF_HEADER" || { echo "not an AArch64 object" >&2; exit 1; }
grep -q '(NEEDED)' <<<"$ELF_DYNAMIC" || { echo "no NEEDED entries" >&2; exit 1; }
if grep -q '(TEXTREL)' <<<"$ELF_DYNAMIC"; then
    echo "text relocation found in $(basename "$OUT")" >&2
    exit 1
fi
grep -q 'pilot_module_init' <<<"$ELF_SYMBOLS" || { echo "pilot_module_init missing" >&2; exit 1; }

MODEL_SYMBOL="pilot_module_model_${MODEL}"
grep -q "$MODEL_SYMBOL" <<<"$ELF_SYMBOLS" || {
    echo "$MODEL_SYMBOL missing (model marker not exported)" >&2
    exit 1
}

# --- glibc guard ------------------------------------------------------------
# The camera ships glibc 2.25.  Anything newer means the wrong headers or
# libraries were used, and the module would fail to dlopen on the camera.
if grep -q '__isoc23_' <<<"$ELF_SYMBOLS"; then
    echo "$(basename "$OUT") references __isoc23_* symbols: built against glibc 2.38+ headers, not the camera sysroot" >&2
    exit 1
fi
MAX_GLIBC="$(readelf -VW "$OUT" | grep -oE 'GLIBC_[0-9]+\.[0-9]+' | sed 's/^GLIBC_//' | sort -uV | tail -1 || true)"
if [ -n "$MAX_GLIBC" ]; then
    major="${MAX_GLIBC%%.*}"
    minor="${MAX_GLIBC##*.}"
    if [ "$major" -gt 2 ] || { [ "$major" -eq 2 ] && [ "$minor" -gt 25 ]; }; then
        echo "$(basename "$OUT") requires GLIBC_$MAX_GLIBC but the camera provides glibc 2.25" >&2
        exit 1
    fi
    echo "glibc requirement: <= GLIBC_$MAX_GLIBC (camera provides 2.25)"
else
    echo "glibc requirement: none"
fi

sha256sum "$OUT"
stat -c '%n %s bytes' "$OUT"
