#!/bin/bash
# Extract a file out of an ext2/3/4 partition image taken from a firmware tar.
#
# Firmware images store the rootfs as a plain ext2 file system, but the files in
# it are not necessarily contiguous: reading N bytes at the file's first data
# offset gives you whatever blocks happen to follow, which is garbage for a
# 90 MB shared object.  Always go through the inode mapping with debugfs.
#
#   tools/cp/dump_ext2_file.sh <partition.img> </lib/appFw.so> <out.so>
#
# Partition layout for ILCE-7CM2 2.01 / ILCE-6700 2.00 lives in
# 0300_partconf/partinf.conf inside firmware.tar; usr is nflasha15, and its
# root already contains /lib (there is no /usr prefix).
set -e
img=$1
remote=$2
out=$3

if [ ! -f "$img" ]; then
    echo "no such image: $img" >&2
    exit 1
fi
if [ ! -f "$out" ]; then
    :
fi

rm -f "$out"
debugfs -R "dump $remote $out" "$img" >/dev/null 2>&1 || true
if [ ! -s "$out" ]; then
    echo "dump failed: $remote not found in $img" >&2
    exit 1
fi

ls -l "$out"
sha256sum "$out"
