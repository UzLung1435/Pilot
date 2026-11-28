#!/usr/bin/env python3
"""Show what surrounds the A6700 / A7 C II cave candidates.

A6700 uses VA 0xC12D68 (file 0xB0AD68) as the code cave its CP patch jumps to.
That address is occupied in the A7 C II image, so the candidate there is
VA 0xC63EA4 (file 0xB5BEA4).  Printing the bytes before and after both regions
tells us whether the zero area sits between code and read-only data -- the
layout A6700 already proved safe -- or somewhere else.
"""
import sys

VABIAS = 0x108000


def dump(data, off, n, label):
    print(f"-- {label} file {off:#08x} va {off + VABIAS:#08x}")
    for row in range(0, n, 16):
        chunk = data[off + row:off + row + 16]
        hexs = " ".join(f"{b:02x}" for b in chunk)
        text = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        print(f"  {off + row + VABIAS:#08x}: {hexs:<48} {text}")


def zero_run(data, off):
    """Length of the zero run starting at off, and total zeros around it."""
    i = off
    while i < len(data) and data[i] == 0:
        i += 1
    return i - off


def main():
    a67 = open(r"work\a6700-unpack\a6700-cpapp-b.bin", "rb").read()
    a7c = open(r"oldProjects\FlashDir\backup1\candidate\cpapp-b.bin", "rb").read()

    cave67 = 0xC12D68 - VABIAS
    cave7c = 0xC63EA4 - VABIAS

    print(f"a6700 cave {cave67:#x} zero run = {zero_run(a67, cave67)} bytes")
    print(f"a7c2  cave {cave7c:#x} zero run = {zero_run(a7c, cave7c)} bytes")
    print()
    dump(a67, cave67 - 0x40, 0x60, "a6700 before cave")
    dump(a67, cave67 + 0x200 - 0x20, 0x60, "a6700 after cave")
    print()
    dump(a7c, cave7c - 0x40, 0x60, "a7c2 before cave")
    dump(a7c, cave7c + 0x2080 - 0x20, 0x60, "a7c2 after cave")


if __name__ == "__main__":
    main()
