#!/usr/bin/env python3
"""Print a 64 KiB non-zero density map of a CP firmware image.

Code segments stay dense; padding and data tables show up as low-density or
text-heavy regions.  VA = file offset + 0x108000.
"""
import sys

CHUNK = 0x10000
BASE = 0x108000


def main():
    path = sys.argv[1]
    lo = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0
    hi = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0xFFFFFFFF
    with open(path, "rb") as f:
        data = f.read()

    for off in range(lo, min(hi, len(data)), CHUNK):
        blk = data[off:off + CHUNK]
        if not blk:
            break
        nz = sum(1 for b in blk if b)
        printable = sum(1 for b in blk if 0x20 <= b < 0x7F)
        print(f"va 0x{off + BASE:08x}  nonzero {nz * 100 // len(blk):3d}%  "
              f"printable {printable * 100 // len(blk):3d}%")


if __name__ == "__main__":
    main()
