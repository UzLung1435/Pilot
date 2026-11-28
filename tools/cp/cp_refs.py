#!/usr/bin/env python3
"""Scan a whole CP image for movw/movt pairs landing in a VA window.

Before reusing a zero-filled region as a code cave we have to prove nothing in
the firmware actually addresses it.  Every absolute address in the Thumb-2
code is materialised by a movw/movt pair, so enumerating all pairs and checking
the target window is a complete audit for that addressing form.

Usage: cp_refs.py <image> <lo_va> <hi_va>
"""
import struct
import sys

VABIAS = 0x108000


def iter_pairs(data, window=32):
    n = len(data)
    i = 0
    while i < n - 4:
        b0 = data[i]
        b1 = data[i + 1]
        # movw T3 prefix: halfword 0xF24n  ->  bytes (0x4n, 0xF2)
        if (b0 & 0xF0) != 0x40 or b1 != 0xF2:
            i += 2
            continue
        hw1 = struct.unpack_from("<H", data, i)[0]
        hw2 = struct.unpack_from("<H", data, i + 2)[0]
        imm4 = hw1 & 0xF
        i_bit = (hw1 >> 10) & 1
        imm3 = (hw2 >> 12) & 0x7
        rd = (hw2 >> 8) & 0xF
        imm8 = hw2 & 0xFF
        lo = (imm4 << 12) | (i_bit << 11) | (imm3 << 8) | imm8
        for j in range(i + 4, min(n - 4, i + 4 + window) + 1, 2):
            if data[j + 1] != 0xF2 or (data[j] & 0xF0) != 0xC0:
                continue
            g1 = struct.unpack_from("<H", data, j)[0]
            g2 = struct.unpack_from("<H", data, j + 2)[0]
            rd2 = (g2 >> 8) & 0xF
            if rd2 != rd:
                continue
            hi = ((g1 & 0xF) << 12) | (((g1 >> 10) & 1) << 11) | \
                 (((g2 >> 12) & 0x7) << 8) | (g2 & 0xFF)
            yield i, rd, (hi << 16) | lo
            break
        i += 2


def main():
    path, lo, hi = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16)
    data = open(path, "rb").read()
    hits = 0
    for off, reg, val in iter_pairs(data):
        if lo <= val < hi:
            hits += 1
            print(f"  ref at file {off:#08x} va {off + VABIAS:#08x} "
                  f"r{reg} -> {val:#010x}")
    print(f"total refs into [{lo:#x},{hi:#x}): {hits}")


if __name__ == "__main__":
    main()
