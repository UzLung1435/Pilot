#!/usr/bin/env python3
"""For every AArch64 ELF found in a disk image, estimate its size from the
section header table and print the large ones."""
import struct
import sys

CHUNK = 1 << 20
MIN_SIZE = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x1000000


def elf_end(pos, head, tail, tail_pos):
    """Return the estimated end offset of the ELF at pos."""
    if len(head) < 64:
        return None
    e_shoff, = struct.unpack_from("<Q", head, 0x28)
    e_shentsize, = struct.unpack_from("<H", head, 0x3A)
    e_shnum, = struct.unpack_from("<H", head, 0x3C)
    e_phoff, = struct.unpack_from("<Q", head, 0x20)
    e_phnum, = struct.unpack_from("<H", head, 0x38)
    ends = [e_phoff + e_phnum * 56, e_shoff + e_shnum * e_shentsize]
    return pos + max(ends)


def main():
    path = sys.argv[1]
    with open(path, "rb") as f:
        data = f.read()
    found = []
    i = 0
    while True:
        i = data.find(b"\x7fELF", i)
        if i < 0:
            break
        e_machine, = struct.unpack_from("<H", data, i + 18)
        if e_machine == 0xB7:
            end = elf_end(i, data[i:i + 64], None, None)
            if end and end > i:
                found.append((i, end, end - i))
        i += 4

    for pos, end, size in found:
        if size >= MIN_SIZE:
            print(f"0x{pos:09x} size~0x{size:x} ({size // 1024} KiB) end 0x{end:09x}")


if __name__ == "__main__":
    main()
