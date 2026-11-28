#!/usr/bin/env python3
"""List entries in an unpacked firmware tar (optionally filtered by name)."""
import sys
import tarfile


def main():
    path = sys.argv[1]
    needle = sys.argv[2] if len(sys.argv) > 2 else ""
    with tarfile.open(path) as tar:
        for m in tar:
            if needle and needle.lower() not in m.name.lower():
                continue
            print(f"{m.size:12d}  {m.name}")


if __name__ == "__main__":
    main()
