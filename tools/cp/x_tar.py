#!/usr/bin/env python3
"""Extract named entries from an unpacked firmware tar."""
import pathlib
import sys
import tarfile


def main():
    tar_path = sys.argv[1]
    out_dir = pathlib.Path(sys.argv[2])
    names = set(sys.argv[3:])
    out_dir.mkdir(parents=True, exist_ok=True)
    with tarfile.open(tar_path) as tar:
        for m in tar:
            if not m.isfile():
                continue
            if names and not any(n in m.name for n in names):
                continue
            dst = out_dir / pathlib.Path(m.name).name
            with tar.extractfile(m) as f, open(dst, "wb") as out:
                out.write(f.read())
            print(f"{m.size:12d}  {m.name} -> {dst}")


if __name__ == "__main__":
    main()
