#!/usr/bin/env python3
"""Scan a disk image for ELF headers and print their basic fields."""
import struct
import sys

CHUNK = 1 << 20


def main():
    path = sys.argv[1]
    wanted = sys.argv[2] if len(sys.argv) > 2 else None  # 'aarch64' etc.
    hits = []
    carry = b""
    base = 0
    with open(path, "rb") as f:
        while True:
            blk = f.read(CHUNK)
            if not blk:
                break
            data = carry + blk
            start = 0
            while True:
                i = data.find(b"\x7fELF", start)
                if i < 0:
                    break
                pos = base - len(carry) + i
                if len(data) - i >= 64:
                    cls, enc = data[i + 4], data[i + 5]
                    e_type, e_machine = struct.unpack_from("<HH", data, i + 16)
                    hits.append((pos, cls, enc, e_type, e_machine))
                start = i + 4
            carry = data[-64:]
            base += len(blk)

    for pos, cls, enc, e_type, e_machine in hits:
        arch = {0xB7: "aarch64", 0x28: "arm", 0x3E: "x86-64"}.get(e_machine, hex(e_machine))
        kind = {2: "EXEC", 3: "DYN"}.get(e_type, str(e_type))
        if wanted and arch != wanted:
            continue
        print(f"0x{pos:09x}  class={cls} enc={enc} {kind:5s} {arch}")


if __name__ == "__main__":
    main()
