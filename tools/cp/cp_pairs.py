#!/usr/bin/env python3
"""Decode every movw/movt pair in a byte range and report the 32-bit constant.

Thumb-2 encodings (T3):

  movw  Rd, #imm16 :  11110 i 10 0100 imm4 | 0 imm3 Rd imm8
  movt  Rd, #imm16 :  11110 i 10 1100 imm4 | 0 imm3 Rd imm8

so the low nibble of halfword 1 is imm4, bit 10 is i, and halfword 2 carries
imm3 in bits 14:12, Rd in 11:8 and imm8 in 7:0.  imm16 = imm4:i:imm3:imm8.

Comparing the same region in the A6700 and A7 C II images yields an absolute
address translation table for their shared code.
"""
import struct
import sys

OFF = 0x108000


def decode(hw1, hw2):
    imm4 = hw1 & 0xF
    i_bit = (hw1 >> 10) & 1
    imm3 = (hw2 >> 12) & 0x7
    rd = (hw2 >> 8) & 0xF
    imm8 = hw2 & 0xFF
    imm16 = (imm4 << 12) | (i_bit << 11) | (imm3 << 8) | imm8
    return rd, imm16


def pairs_in(data, start, end, window=32):
    out = []
    for i in range(start, min(end, len(data) - 4) + 1, 2):
        hw1 = struct.unpack_from("<H", data, i)[0]
        if (hw1 & 0xFFF0) != 0xF240:          # movw only
            continue
        hw2 = struct.unpack_from("<H", data, i + 2)[0]
        rd, lo = decode(hw1, hw2)
        for j in range(i + 4, min(end, i + 4 + window) + 1, 2):
            g1 = struct.unpack_from("<H", data, j)[0]
            if (g1 & 0xFFF0) != 0xF2C0:
                continue
            g2 = struct.unpack_from("<H", data, j + 2)[0]
            rd2, hi = decode(g1, g2)
            if rd2 == rd:
                out.append((i, rd, (hi << 16) | lo))
                break
    return out


def main():
    a67 = open(r"work\a6700-unpack\a6700-cpapp-b.bin", "rb").read()
    a7c = open(r"oldProjects\FlashDir\backup1\candidate\cpapp-b.bin", "rb").read()
    lo = int(sys.argv[1], 16) if len(sys.argv) > 1 else 0x74A0C0
    hi = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x74A2C0
    shift = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x1B4C0
    for tag, data, base in (("a6700", a67, lo), ("a7c2", a7c, lo + shift)):
        print(f"--- {tag} file {base:#08x} va {base + OFF:#08x}")
        for off, reg, val in pairs_in(data, base, base + (hi - lo)):
            print(f"  {off + OFF:#08x} r{reg} = {val:#010x}")


if __name__ == "__main__":
    main()
