#!/usr/bin/env bash
set -euo pipefail

pilot_root=$(cd "$(dirname "$0")/.." && pwd)
pilot_sysroot=${PILOT_SYSROOT:?set PILOT_SYSROOT to a target sysroot}
pilot_cc=${PILOT_CC:-aarch64-linux-gnu-gcc-11}
pilot_python=${PILOT_PYTHON:-python3}
pilot_model=${PILOT_MODEL:-a6700}
pilot_models="$pilot_root/tools/pilot_tool/models.py"

# A single registry defines the model macro, the DSO soname and the exports.
"$pilot_python" "$pilot_models" --check >/dev/null
pilot_value() { "$pilot_python" "$pilot_models" "$pilot_model" "$1"; }
pilot_build_macro="$(pilot_value build_macro)"
pilot_soname="$(pilot_value dso_soname)"
pilot_output=${PILOT_OUT:-$pilot_root/build/$pilot_soname}
pilot_build=$(mktemp -d /tmp/pilot-dso-build.XXXXXX)
trap 'rm -rf "$pilot_build"' EXIT

for pilot_input in \
    "$pilot_root/patch/pilot_entry.c" \
    "$pilot_root/patch/pilot_fsys.c" \
    "$pilot_root/patch/pilot_time.c" \
    "$pilot_root/patch/pilot_api.c" \
    "$pilot_root/patch/pilot_memory.c" \
    "$pilot_root/patch/pilot_cp_mem.c" \
    "$pilot_root/patch/pilot_module_dir.c" \
    "$pilot_root/patch/pilot_module_files.c" \
    "$pilot_root/patch/pilot_modules.c"; do
    pilot_object="$pilot_build/$(basename "${pilot_input%.c}").o"
    "$pilot_cc" --sysroot="$pilot_sysroot" \
        -std=c11 -O2 -march=armv8-a -mno-outline-atomics \
        -fno-stack-protector -fPIC -fvisibility=hidden \
        -ffunction-sections -fdata-sections \
        -Wall -Wextra -Werror -D"$pilot_build_macro" \
        -I "$pilot_root/patch" -c "$pilot_input" -o "$pilot_object"
done

# The DSO is uploaded through the Jiritsu file command, which caps a single
# file at 64 KiB.  The link step therefore has to stay inside that budget:
# drop unused sections and the symbol table, and pick a linker that packs the
# loadable segments instead of padding them out to the 64 KiB page size
# (binutils pads to max-page-size, which alone costs ~24 KiB of holes here).
pilot_link_flags=(
    -Wl,--build-id=none -Wl,--hash-style=gnu
    -Wl,--gc-sections -Wl,--strip-all
)
if "$pilot_cc" -fuse-ld=lld -shared -Wl,-soname,pilot_ld_probe.so \
        -o /dev/null -x c /dev/null 2>/dev/null; then
    pilot_link_flags+=(-fuse-ld=lld)
else
    # Without lld the RELRO segment lands past the 64 KiB boundary; keeping the
    # GOT writable is the smaller price than failing the upload limit.
    pilot_link_flags+=(-Wl,-z,norelro)
fi

mkdir -p "$(dirname "$pilot_output")"
"$pilot_cc" --sysroot="$pilot_sysroot" -shared -nostdlib \
    -Wl,-soname,"$pilot_soname" -Wl,--no-undefined -Wl,--no-as-needed \
    "${pilot_link_flags[@]}" \
    -L"$pilot_sysroot/lib" -o "$pilot_output" "$pilot_build"/*.o \
    -lgcc -l:libdl.so.2 -l:libpthread.so.0 -l:libc.so.6

pilot_limit=65536
pilot_size=$(stat -c %s "$pilot_output")
if [ "$pilot_size" -gt "$pilot_limit" ]; then
    echo "Pilot DSO is $pilot_size bytes; the Jiritsu upload limit is $pilot_limit" >&2
    exit 1
fi
echo "Pilot DSO size=$pilot_size limit=$pilot_limit"

pilot_symbols="$(readelf -sW "$pilot_output")"
for pilot_symbol in "$(pilot_value mount_callback_symbol)" \
                    "$(pilot_value marker_symbol)" \
                    "$(pilot_value get_api_symbol)"; do
    grep -qF "$pilot_symbol" <<<"$pilot_symbols"
done
for pilot_other in $("$pilot_python" "$pilot_models" --ids); do
    if [ "$pilot_other" = "$pilot_model" ]; then continue; fi
    if grep -qF "pilot_${pilot_other}_v1_marker" <<<"$pilot_symbols"; then
        echo "Pilot DSO built for $pilot_model exports a marker belonging to $pilot_other" >&2
        exit 1
    fi
done

echo "Pilot model=$pilot_model soname=$pilot_soname"
sha256sum "$pilot_output"
stat -c 'size_bytes=%s' "$pilot_output"
