#!/usr/bin/env bash
# Checks the menu UI module without a camera.
#
#   host model test   the A7C II content tree / editor / pin lifetime, against
#                     the vendored registry (no appFw, any host compiler)
#   offset verifier   every hardcoded A7C II address against a stock appFw.so
#   cross build       the loadable DSO, with the guest glibc version gate
#
# The offset verifier needs an ILCE-7CM2 2.01 appFw.so dump; set
# PILOT_A7C2_IMAGE to point at one, otherwise the check reports "skipped".
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
MODULE=$(cd "$HERE/.." && pwd)
ROOT=$(cd "$MODULE/../.." && pwd)
TMP=$(mktemp -d /tmp/pilot-menu-ui-test.XXXXXX)
trap 'rm -rf "$TMP"' EXIT

CC=${HOST_CC:-cc}
echo "== host model test"
"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    -o "$TMP/model" "$HERE/test_a7c2_model.c" "$MODULE/a7c2/vendor/menu_api.c"
"$TMP/model"

echo
echo "== offset verifier"
IMAGE=${PILOT_A7C2_IMAGE:-$ROOT/../oldProjects/FlashDir/backup1.3/original/appFw.so.bin}
if [ -f "$IMAGE" ]; then
    python3 "$HERE/verify_a7c2_offsets.py" --image "$IMAGE"
else
    echo "skipped: no stock image at $IMAGE (set PILOT_A7C2_IMAGE)"
fi

echo
echo "== cross build (PILOT_MODEL=a7c2)"
if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    PILOT_MODEL=a7c2 bash "$MODULE/build.sh" "$TMP/pilot_menu_ui_a7c2.so"
    SYMBOLS=$(aarch64-linux-gnu-readelf -sW "$TMP/pilot_menu_ui_a7c2.so")
    grep -q pilot_module_model_a7c2 <<<"$SYMBOLS"
    grep -q pilot_module_init <<<"$SYMBOLS"
else
    echo "skipped: aarch64-linux-gnu-gcc not found"
fi

echo
echo "menu ui: all checks passed"
