#!/usr/bin/env bash
set -euo pipefail

pilot_root=$(cd "$(dirname "$0")/../.." && pwd)
pilot_sysroot=${PILOT_SYSROOT:?set PILOT_SYSROOT to a target sysroot}
pilot_cc=${PILOT_CC:-aarch64-linux-gnu-gcc-11}
pilot_qemu=${PILOT_QEMU:-qemu-aarch64}
pilot_dso=${PILOT_DSO:-$pilot_root/build/pilot.so}
pilot_probe_module=${PILOT_PROBE_MODULE:-}
pilot_probe_appfw=${PILOT_PROBE_APPFW:-}
pilot_probe_module_name=${pilot_probe_module##*/}
pilot_test_root=$(mktemp -d /tmp/pilot-runtime-smoke.XXXXXX)

if [ -e /tmp_wbshd ]; then
    echo "refusing to use existing /tmp_wbshd in the host test environment" >&2
    exit 2
fi
mkdir /tmp_wbshd

pilot_cleanup() {
    rm -f /tmp_wbshd/pilot/modules/a6700_pilot_api_smoke.so
    if [ -n "$pilot_probe_module_name" ]; then
        rm -f "/tmp_wbshd/pilot/modules/$pilot_probe_module_name"
    fi
    rm -f /tmp_wbshd/pilot/pilot-runtime.log
    rm -f /tmp/pilot-module-init.marker
    rmdir /tmp_wbshd/pilot/modules 2>/dev/null || true
    rmdir /tmp_wbshd/pilot 2>/dev/null || true
    rmdir /tmp_wbshd 2>/dev/null || true
    rm -rf "$pilot_test_root"
}
trap pilot_cleanup EXIT

# This smoke test exercises the ILCE-6700 build only.  The model macro must
# match the DSO under test so the harness dlsym()s the right export names.
pilot_model_macro=PILOT_MODEL_A6700

"$pilot_cc" --sysroot="$pilot_sysroot" -std=c11 -O2 -march=armv8-a \
    -mno-outline-atomics -fno-stack-protector -fPIC -fvisibility=hidden \
    -D"$pilot_model_macro" \
    -shared -nostdlib -Wl,-soname,a6700_pilot_api_smoke.so \
    -Wl,--no-undefined -Wl,--no-as-needed -L"$pilot_sysroot/lib" \
    -o "$pilot_test_root/a6700_pilot_api_smoke.so" \
    "$pilot_root/patch/tests/pilot_api_smoke_module.c" \
    -l:libc.so.6 -static-libgcc
readelf -sW "$pilot_test_root/a6700_pilot_api_smoke.so" | grep -q 'pilot_module_model_a6700'

"$pilot_cc" --sysroot="$pilot_sysroot" -std=c11 -O2 -march=armv8-a \
    -mno-outline-atomics -fno-stack-protector \
    -Wl,-dynamic-linker,/lib/ld-linux-aarch64.so.1 \
    -Wl,-rpath-link,"$pilot_sysroot/lib" -L"$pilot_sysroot/lib" \
    -Wl,--export-dynamic -Wl,--no-as-needed \
    -Wl,--allow-shlib-undefined -o "$pilot_test_root/pilot_sd_log_smoke" \
    "$pilot_root/patch/tests/pilot_sd_log_smoke.c" \
    -l:libdl.so.2 -l:libpthread.so.0 -l:libc.so.6 -static-libgcc

"$pilot_cc" --sysroot="$pilot_sysroot" -std=c11 -O2 -march=armv8-a \
    -mno-outline-atomics -fno-stack-protector \
    -DPILOT_SMOKE_OMIT_FSYS_SEEK \
    -Wl,-dynamic-linker,/lib/ld-linux-aarch64.so.1 \
    -Wl,-rpath-link,"$pilot_sysroot/lib" -L"$pilot_sysroot/lib" \
    -Wl,--export-dynamic -Wl,--no-as-needed \
    -Wl,--allow-shlib-undefined -o "$pilot_test_root/pilot_sd_log_smoke_missing" \
    "$pilot_root/patch/tests/pilot_sd_log_smoke.c" \
    -l:libdl.so.2 -l:libpthread.so.0 -l:libc.so.6 -static-libgcc

for pilot_scenario in allow disable error status-only fuse-armed dtinit; do
    "$pilot_qemu" -L "$pilot_sysroot" \
        "$pilot_test_root/pilot_sd_log_smoke" \
        "$pilot_dso" \
        "$pilot_test_root/card-$pilot_scenario" \
        "$pilot_test_root/a6700_pilot_api_smoke.so" \
        "$pilot_scenario"
done

# A completed traversal stays enabled on a later startup with the same package.
"$pilot_qemu" -L "$pilot_sysroot" \
    "$pilot_test_root/pilot_sd_log_smoke" \
    "$pilot_dso" \
    "$pilot_test_root/card-allow" \
    "$pilot_test_root/a6700_pilot_api_smoke.so" \
    allow

# The first run converts the seeded interrupted-load record into a persistent
# trip. A second process must keep the unchanged package quarantined.
"$pilot_qemu" -L "$pilot_sysroot" \
    "$pilot_test_root/pilot_sd_log_smoke" \
    "$pilot_dso" \
    "$pilot_test_root/card-fuse-armed" \
    "$pilot_test_root/a6700_pilot_api_smoke.so" \
    fuse-armed

"$pilot_qemu" -L "$pilot_sysroot" \
    "$pilot_test_root/pilot_sd_log_smoke_missing" \
    "$pilot_dso" \
    "$pilot_test_root/card-resolve" \
    "$pilot_test_root/a6700_pilot_api_smoke.so" \
    resolve

if [ -n "$pilot_probe_module" ]; then
    probe_appfw_args=()
    if [ -n "$pilot_probe_appfw" ]; then
        probe_appfw_args+=("$pilot_probe_appfw")
    fi
    "$pilot_qemu" -L "$pilot_sysroot" \
        "$pilot_test_root/pilot_sd_log_smoke" \
        "$pilot_dso" \
        "$pilot_test_root/card-menu-probe" \
        "$pilot_probe_module" \
        probe "${probe_appfw_args[@]}"
fi
