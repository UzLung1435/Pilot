#!/usr/bin/env python3
"""Locate the A6700 cpapp-b.bin inside the firmware tar by its loader header.

The CP image begins with a 16-byte ARM loader stub:

  04 10 9f e5   ldr r1, [pc, #4]
  04 10 81 e2   add r1, r1, #4
  01 f0 a0 e1   mov pc, r1
  0c 80 10 00   .word 0x0010800c

Every candidate is extracted and hashed against the sha256 that
patch.a6700.json records for Sony ILCE-6700 2.00 (61b7fe5c...).
"""
import hashlib
import sys
import tarfile

LOADER = bytes.fromhex("04109fe5041081e201f0a0e1")
LEN = 17892024
NEED = hashlib.sha256
WANT = "61b7fe5c0ce227ea865d799b81fe77ae359b7c5dcd39d13c9c2241d9cad23559"
CHUNK = 1 << 22


def main():
    tar_path, out_dir = sys.argv[1], sys.argv[2]
    with tarfile.open(tar_path) as t:
        for m in t.getmembers():
            if not m.isfile() or not m.name.startswith("0700_part_image/"):
                continue
            if m.size < LEN:
                continue
            print(f"scanning {m.name} ({m.size})", flush=True)
            f = t.extractfile(m)
            carry = b""
            base = 0
            hits = []
            while True:
                chunk = f.read(CHUNK)
                if not chunk:
                    break
                buf = carry + chunk
                i = 0
                while True:
                    j = buf.find(LOADER, i)
                    if j < 0:
                        break
                    hits.append(base + j - len(carry))
                    i = j + 1
                carry = buf[-(len(LOADER) - 1):]
                base += len(chunk)
            if not hits:
                continue
            print(f"  loader hits at {[hex(h) for h in hits[:10]]}", flush=True)
            for off in hits:
                if m.size - off < LEN:
                    continue
                f.seek(base)
                f.seek(off)
                blob = f.read(LEN)
                if len(blob) != LEN:
                    continue
                h = hashlib.sha256(blob).hexdigest()
                tag = "MATCH" if h == WANT else "no"
                print(f"  off={off:#x} sha={h[:16]} {tag}", flush=True)
                if h == WANT:
                    name = f"{out_dir}/a6700-cpapp-b.bin"
                    with open(name, "wb") as o:
                        o.write(blob)
                    print(f"  wrote {name}", flush=True)


if __name__ == "__main__":
    main()
