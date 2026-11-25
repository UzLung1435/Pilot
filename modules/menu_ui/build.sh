#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MODULE_DIR="$ROOT/modules/menu_ui"
BUILD_DIR="$ROOT/build"

# The module is body specific either way: the ILCE-6700 build hardcodes the
# 2.00 menu table and its screen factories, the ILCE-7CM2 build hardcodes the
# 2.01 menu record, list transport and patch slots.  Both are recovered from
# one stock firmware image, so building for the wrong body is refused here and
# again at load time by the pilot_module_model_* tag.
MODEL="${PILOT_MODEL:-a6700}"
case "$MODEL" in
a6700)
    CC="${AARCH64_CC:-aarch64-linux-gnu-gcc-11}"
    SYSROOT="${PILOT_A6700_SYSROOT:-/home/cameraemu/.cache/a6700-kernel-20230712/a6700-original-boot/pilot-merged-root-test}"
    MODEL_MACRO=PILOT_MODEL_A6700
    SONAME=pilot_menu_ui.so
    DEFAULT_OUT="$BUILD_DIR/pilot_menu_ui.so"
    SOURCES=("$MODULE_DIR/pilot_menu_ui.c")
    INCLUDES=(-I "$ROOT/patch")
    MODEL_SYMBOL=pilot_module_model_a6700
    MAX_GLIBC_MINOR=25
    ;;
a7c2)
    CC="${AARCH64_CC:-aarch64-linux-gnu-gcc}"
    SYSROOT="${PILOT_A7C2_SYSROOT:-}"
    MODEL_MACRO=PILOT_MODEL_A7C2
    SONAME=pilot_menu_ui_a7c2.so
    DEFAULT_OUT="$BUILD_DIR/pilot_menu_ui_a7c2.so"
    SOURCES=("$MODULE_DIR/a7c2/pilot_menu_ui_a7c2.c" "$MODULE_DIR/a7c2/vendor/menu_api.c")
    INCLUDES=(-I "$ROOT/patch" -I "$MODULE_DIR/a7c2")
    MODEL_SYMBOL=pilot_module_model_a7c2
    MAX_GLIBC_MINOR=25
    ;;
*)
    echo "unknown PILOT_MODEL=$MODEL (expected a6700 or a7c2)" >&2
    exit 2
    ;;
esac

OUT="${1:-$DEFAULT_OUT}"

SYSROOT_FLAGS=()
if [ -n "$SYSROOT" ]; then
    if [ ! -d "$SYSROOT" ]; then
        echo "sysroot not found: $SYSROOT" >&2
        exit 2
    fi
    SYSROOT_FLAGS=(--sysroot="$SYSROOT" -L"$SYSROOT/lib")
fi

TMP="$(mktemp -d /tmp/pilot-menu-ui.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$(dirname "$OUT")"

OBJECTS=()
for src in "${SOURCES[@]}"; do
    obj="$TMP/$(basename "${src%.c}").o"
    # -fno-stack-protector: the guest libc has no __stack_chk_guard for us and
    # the stock modules are built the same way.
    "$CC" -std=c11 -O2 -march=armv8-a -mno-outline-atomics \
        -fno-stack-protector -fPIC -fvisibility=hidden -Wall -Wextra -Werror \
        -D"$MODEL_MACRO" "${INCLUDES[@]}" "${SYSROOT_FLAGS[@]}" -c "$src" -o "$obj"
    OBJECTS+=("$obj")
done

"$CC" -shared -nostdlib -Wl,-soname,"$SONAME" \
    -Wl,--no-undefined -Wl,--no-as-needed -Wl,--build-id=none \
    "${SYSROOT_FLAGS[@]}" -o "$OUT" "${OBJECTS[@]}" \
    -l:libpthread.so.0 -l:libdl.so.2 -l:libc.so.6 -lgcc

ELF_HEADER="$(readelf -h "$OUT")"
ELF_DYNAMIC="$(readelf -d "$OUT")"
ELF_SYMBOLS="$(readelf -sW "$OUT")"
grep -q AArch64 <<<"$ELF_HEADER"
grep -q '(NEEDED)' <<<"$ELF_DYNAMIC"
if grep -q '(TEXTREL)' <<<"$ELF_DYNAMIC"; then
    echo "text relocation found in $SONAME" >&2
    exit 1
fi
grep -q 'pilot_module_init' <<<"$ELF_SYMBOLS"
grep -q "$MODEL_SYMBOL" <<<"$ELF_SYMBOLS"

# The body runs glibc 2.25; a newer build host happily emits GLIBC_2.34 version
# references that only fail at dlopen() time on the camera.
if readelf -V "$OUT" | grep -Eq "GLIBC_2\\.($(seq 26 $((MAX_GLIBC_MINOR + 40)) | paste -sd'|'))"; then
    echo "guest libc is 2.$(($MAX_GLIBC_MINOR)) but the build needs a newer GLIBC_* symbol version" >&2
    exit 1
fi

sha256sum "$OUT"
stat -c '%n %s bytes' "$OUT"
