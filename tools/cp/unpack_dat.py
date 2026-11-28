#!/usr/bin/env python3
"""Unpack Sony FX2 1.01 BODYDATA.DAT: DAT container -> FDAT (decrypt) -> firmware.tar + updater.img

Implements the .DAT chunk parser and the 4th-gen (CXD90057_k8 / CXD90045 / CXD90014)
FDAT block decryption from ma1co/fwtool.py PR52 (pinned 3955d789), standalone.
"""
from __future__ import annotations

import io
import struct
import sys
import zlib
from pathlib import Path

from Cryptodome.Cipher import AES

DAT_MAGIC = b"\x89\x55\x46\x55\x0d\x0a\x1a\x0a"
FDAT_MAGIC = b"UDTRFIRM"

# keys from fwtool/sony/constants.py (PR52)
KEY_AES = b"\xE3\xB0\xC4\x42\x98\xFC\x1C\x14\x9A\xFB\xF4\xC8\x99\x6F\xB9\x24"
KEY_CXD90057_K8 = b"\x43\x45\x89\x43\x2D\x31\x90\x10\xF1\x13\x27\x27\x35\x32\x86\xCE\x4D\x7E\xDE\x8B\x9C\x4F\x72\x17\xA0\xE0\x22\x6E\x0F\x0E\xDC\x69"
KEY_CXD90057_K0 = b"\x37\x6A\x61\xD9\x07\x03\x5C\xF1\x83\x00\xF2\xAD\xD6\x50\x3E\x64\x9F\xCC\xF0\xC0\x19\x1D\xCC\x38\xC3\xB0\x58\x95\x15\x0A\x7F\xB6"
KEY_CXD90045_K8 = b"\x78\x2F\xD9\x79\x56\x68\x6B\x83\x21\xEA\xC7\xAB\xC2\xEA\x68\xDC\x8E\xC5\x3E\x4B\x75\xF1\x6B\x78\x51\x74\x0B\x1D\xC1\xC0\x13\xFB"
KEY_CXD90045_K0 = b"\xC1\xAA\x8F\x7C\x46\x34\x1F\xFE\xD1\x55\x89\xFC\x81\x70\xA6\xBB\x59\x25\xE8\x5F\x62\x82\xD7\xF9\x5B\xA3\xFD\xF5\xD3\x03\xE0\x6B"
KEY_CXD90014 = b"\xE8\xB0\x88\x6D\x97\x18\x4F\x1F\x65\xC7\x67\xF7\x93\x99\x65\xBF"

CRYPTERS = [
    # (name, key2, shape): shape 'cbc' = AesCbcCrypter(key_aes, key2),
    # 'ecb' = AesCrypter(key_aes).  FX2 1.01 uses cbc with cxd90057_k8.
    ("CXD90057_k8", KEY_CXD90057_K8, "cbc"),
    ("CXD90057_k0", KEY_CXD90057_K0, "cbc"),
    ("CXD90045_k8", KEY_CXD90045_K8, "cbc"),
    ("CXD90045_k0", KEY_CXD90045_K0, "cbc"),
    ("CXD90014", KEY_CXD90014, "double"),
]

BLOCK = 1024
TAIL = 0x110  # 16-byte IV + 256 trailing


def parse_dat_chunks(path: Path):
    """Yield (type, offset, size) for each chunk in the .DAT container."""
    with open(path, "rb") as f:
        header = f.read(8)
        if header != DAT_MAGIC:
            raise SystemExit(f"not a .DAT file: {header.hex()}")
        offset = 8
        chunks = []
        while True:
            f.seek(offset)
            hdr = f.read(8)
            if len(hdr) < 8:
                break
            size, ctype = struct.unpack(">I4s", hdr)
            chunks.append((ctype, offset + 8, size))
            offset += 8 + size
            if ctype == b"DEND":
                break
        return chunks


def sum16(data: bytes) -> int:
    if len(data) % 2:
        data = data + b"\x00"
    total = 0
    for i in range(0, len(data), 2):
        total += data[i] | (data[i + 1] << 8)
    return total & 0xFFFF


def _decrypt_stream(src, dst, key2: bytes, shape: str):
    """Decrypt the FDAT body block-by-block and unpack payloads into dst.

    shape 'cbc'  : AesCbcCrypter -- first block ECB(key_aes) over [0:512]
                   then CBC(key2, IV) over [512:1024]; later blocks CBC.
    shape 'ecb'  : AesCrypter -- every block ECB(key_aes).
    """
    ecb = AES.new(KEY_AES, AES.MODE_ECB)
    if shape == "double":
        c2 = AES.new(KEY_CXD90014, AES.MODE_ECB)
    else:
        cbc = AES.new(key2, AES.MODE_CBC, iv)

    first = True
    blocks = 0
    while True:
        blk = src.read(BLOCK)
        if not blk:
            break
        if len(blk) != BLOCK:
            raise SystemExit(f"short block at {blocks}: {len(blk)}")
        if shape == "double":
            dec1 = ecb.decrypt(blk)
            dec2 = c2.decrypt(dec1)
            dec = dec1[:512] + dec2[512:] if first else dec2
        elif shape == "ecb":
            dec = ecb.decrypt(blk)
        elif first:
            dec = ecb.decrypt(blk[:512]) + cbc.decrypt(blk[512:])
        else:
            dec = cbc.decrypt(blk)
        first = False
        # unpack block: [0:2] sum16, [2:4] sizeAndEnd, payload
        checksum = struct.unpack_from("<H", dec, 0)[0]
        size_and_end = struct.unpack_from("<H", dec, 2)[0]
        size = size_and_end & 0x7FFF
        if sum16(dec[2:]) != checksum:
            raise SystemExit(f"block {blocks}: wrong checksum")
        dst.write(dec[4:4 + size])
        blocks += 1
    return blocks


iv = b""  # set by caller before _decrypt_stream


def try_decrypter(dat_path: Path, fdat_off: int, fdat_size: int, name: str, key2: bytes, shape: str):
    """Try one crypter on the first block; the FDAT magic sits at payload[0:8]."""
    global iv
    with open(dat_path, "rb") as f:
        f.seek(fdat_off)
        blob = f.read(fdat_size)
    if len(blob) < TAIL:
        raise SystemExit("FDAT chunk too small")
    body = blob[:-TAIL]
    iv = blob[-TAIL:-TAIL + 16]
    first = body[:BLOCK]
    ecb = AES.new(KEY_AES, AES.MODE_ECB)
    if shape == "ecb":
        dec = ecb.decrypt(first)
    else:
        cbc = AES.new(key2, AES.MODE_CBC, iv)
        dec = ecb.decrypt(first[:512]) + cbc.decrypt(first[512:])
    return dec[4:12] == FDAT_MAGIC


def main():
    dat_path = Path(sys.argv[1] if len(sys.argv) > 1 else
                    r"D:\Code\Sony_Cam_Compare\FX2_1.01_Firmware_BODYDATA.DAT")
    out_dir = Path(sys.argv[2] if len(sys.argv) > 2 else
                   r"D:\Code\Sony_Cam_Compare\work\fx2-unpack")
    out_dir.mkdir(parents=True, exist_ok=True)

    chunks = parse_dat_chunks(dat_path)
    print("DAT chunks:")
    for ctype, off, size in chunks:
        print(f"  {ctype.decode(errors='replace')}  off=0x{off:x}  size={size}")

    fdat = next(((o, s) for t, o, s in chunks if t == b"FDAT"), None)
    if not fdat:
        raise SystemExit("no FDAT chunk")
    fdat_off, fdat_size = fdat

    # find the right crypter by probing the first block
    chosen = None
    for name, key2, shape in CRYPTERS:
        if try_decrypter(dat_path, fdat_off, fdat_size, name, key2, shape):
            chosen = (name, key2, shape)
            break
    if not chosen:
        raise SystemExit("no crypter matched (magic UDTRFIRM not found)")
    name, key2, shape = chosen
    print(f"crypter: {name} ({shape})")

    # full decrypt + unpack into plaintext FDAT
    plain_path = out_dir / "fdat_plain.bin"
    global iv
    with open(dat_path, "rb") as f, open(plain_path, "wb") as out:
        f.seek(fdat_off)
        blob = f.read(fdat_size)
        body = blob[:-TAIL]
        iv = blob[-TAIL:-TAIL + 16]
        n = _decrypt_stream(io.BytesIO(body), out, key2, shape)
    print(f"decrypted {n} blocks -> {plain_path} ({plain_path.stat().st_size} bytes)")

    # parse FdatHeader
    with open(plain_path, "rb") as f:
        hdr = f.read(0x40 + 28 * 16)
        magic = hdr[:8]
        if magic != FDAT_MAGIC:
            raise SystemExit(f"bad FDAT magic: {magic.hex()}")
        checksum, version = struct.unpack_from("<I4s", hdr, 8)
        mode_type = hdr[16:17]
        luw_flag = hdr[20:21]
        version_minor, version_major = hdr[32], hdr[33]
        model, region = struct.unpack_from("<ii", hdr, 36)
        fw_off, fw_size, num_fs = struct.unpack_from("<iii", hdr, 48)
        print(f"FDAT: model=0x{model & 0xffffffff:08x} region={region} "
              f"version={version_major:x}.{version_minor:02x} mode={mode_type} luw={luw_flag}")
        print(f"  firmware: off=0x{fw_off:x} size=0x{fw_size:x}")
        fs_headers = []
        for i in range(num_fs if 0 < num_fs <= 28 else 28):
            base = 0x40 + i * 16
            mt = hdr[base:base + 1]
            off, size = struct.unpack_from("<ii", hdr, base + 4)
            if mt in (b"U", b"P") and size:
                fs_headers.append((mt.decode(), off, size))
                print(f"  fs[{i}] type={mt.decode()} off=0x{off:x} size=0x{size:x}")
        if not fs_headers:
            raise SystemExit("no filesystem entry")

        # extract firmware.tar and the first user filesystem
        with open(out_dir / "firmware.tar", "wb") as o:
            f.seek(fw_off)
            remaining = fw_size
            while remaining > 0:
                chunk = f.read(min(1 << 20, remaining))
                if not chunk:
                    break
                o.write(chunk)
                remaining -= len(chunk)
        mt, off, size = fs_headers[0]
        with open(out_dir / f"updater_{mt}.img", "wb") as o:
            f.seek(off)
            remaining = size
            while remaining > 0:
                chunk = f.read(min(1 << 20, remaining))
                if not chunk:
                    break
                o.write(chunk)
                remaining -= len(chunk)
    print("wrote firmware.tar and updater image")


if __name__ == "__main__":
    main()
