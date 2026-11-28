#!/usr/bin/env python3
"""Find the AArch64 ELF header that contains a given file offset, and report
the file size implied by its section header table."""
import struct
import sys


def main():
    path = sys.argv[1]
    pos = int(sys.argv[2], 16)
    with open(path, "rb") as f:
        data = f.read(pos + 1)

    i = data.rfind(b"\x7fELF", 0, pos)
    while i >= 0:
        e_machine, = struct.unpack_from("<H", data, i + 18)
        if e_machine == 0xB7:
            break
        i = data.rfind(b"\x7fELF", 0, i)

    if i < 0:
        print("no ELF header found before the offset")
        return

    e_shoff, = struct.unpack_from("<Q", data, i + 0x28)
    e_shentsize, = struct.unpack_from("<H", data, i + 0x3A)
    e_shnum, = struct.unpack_from("<H", data, i + 0x3C)
    size = e_shoff + e_shnum * e_shentsize
    print(f"elf at 0x{i:x}, section table size estimate 0x{size:x} "
          f"({size / 1048576:.1f} MiB), end 0x{i + size:x}")


if __name__ == "__main__":
    main()
