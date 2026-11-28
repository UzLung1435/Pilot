#!/usr/bin/env python3
"""Locate the uipc receiver hook pattern in CP firmware images.

The A6700 patch site is `30 78 18 b1` (ldrb r0,[r6]; cbz r0, ...).  Scanning
other models tells us whether the pattern is a stable anchor or specific to
one firmware generation.
"""
import sys

PATTERN = bytes.fromhex("307818b1")


def scan(path):
    with open(path, "rb") as f:
        data = f.read()
    hits = []
    start = 0
    while True:
        idx = data.find(PATTERN, start)
        if idx < 0:
            break
        hits.append(idx)
        start = idx + 1
    return data, hits


def main():
    for path in sys.argv[1:]:
        data, hits = scan(path)
        print(f"{path}")
        print(f"  size={len(data)} (0x{len(data):x})")
        for off in hits:
            ctx = data[off:off + 16]
            print(f"  hit file=0x{off:08x} va=0x{off + 0x108000:08x} "
                  f"bytes={ctx.hex()}")
        if not hits:
            print("  no hit")
        print()


if __name__ == "__main__":
    main()
