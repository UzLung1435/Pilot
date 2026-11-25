"""Replay the firmware's view_string update through its native C-string path.

The complete native update/render function runs in Unicorn. External graphics
methods are intercepted; the original branch choosing RID vs C string runs.
The firmware's RTTI, layout identities and widget methods are checked first.
"""

import argparse
import struct
from pathlib import Path

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm64_const import (
    UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_LR,
    UC_ARM64_REG_PC, UC_ARM64_REG_SP, UC_ARM64_REG_X2,
)


def verify(path):
    image = Path(path).read_bytes()
    with Path(path).open("rb") as stream:
        elf = ELFFile(stream)
        segments = [s.header for s in elf.iter_segments()
                    if s["p_type"] == "PT_LOAD"]

    def read(address, size):
        for segment in segments:
            start = segment["p_vaddr"]
            if start <= address and address + size <= start + segment["p_filesz"]:
                offset = address - start + segment["p_offset"]
                return image[offset:offset + size]
        raise ValueError(hex(address))

    def qword(address):
        return struct.unpack("<Q", read(address, 8))[0]

    def string(address):
        result = bytearray()
        while True:
            value = read(address + len(result), 1)
            if value == b"\0":
                return result.decode("ascii")
            result.extend(value)

    bindings = (
        ("LCD", 0x4C6DBF8, 0x25D8960, 0x4EF70A8, 0x50749F0, 391,
         "No1339_FooterTouchBackgroundDefocusSettingBeltValueName",
         "22scr0132_0001_presenter"),
        ("EVF", 0x4C74600, 0x2650B08, 0x4E91820, 0x4FD96B0, 263,
         "No583_IntervalshootingcancelguideMessage",
         "22scr0132_0002_presenter"),
    )
    for kind, slot, update, got, layout, idx, name, presenter in bindings:
        assert qword(slot) == update
        assert string(qword(qword(slot - 24) + 8)) == presenter
        assert qword(got) == layout
        record = layout + idx * 0x60
        assert qword(record) == 0x8343B90
        assert qword(record + 8) == idx
        assert string(qword(record + 16)) == name
        print(f"{kind} presenter/layout/string slot {idx}: OK")

    for layout, idx in ((0x50749F0, 39), (0x4FD96B0, 27)):
        assert read(layout + idx * 0x60 + 0x40, 8) == struct.pack(
            "<II", 0x0207A018, 0x0207A000)

    vtable = 0x4A072A8
    assert qword(vtable + 0x140) == 0x19A61B0
    assert qword(vtable + 0x150) == 0x19A78E8
    assert read(0x19A61B0, 8) == bytes.fromhex("00400791c0035fd6")

    def run(kind, payload, visible=1, use_cstr=True):
        emulator = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        pages = set()

        def write(address, data):
            for page in range(address & ~4095,
                              (address + len(data) + 4095) & ~4095, 4096):
                if page not in pages:
                    emulator.mem_map(page, 4096)
                    pages.add(page)
            emulator.mem_write(address, bytes(data))

        def write_qword(address, value):
            write(address, struct.pack("<Q", value))

        write(0x19A6000, read(0x19A6000, 0x2000))
        write(0x1769000, bytes(0x5000))
        write(0x4A07000, read(0x4A07000, 0x1000))
        for got in (0x4E7F3E0, 0x4EDB388, 0x4E59E98):
            target = qword(got)
            write_qword(got, target)
            write(target, bytes(32))

        # Empty native font metadata tables; native code supplies line metrics.
        write(0x8342000, bytes(0x1000))
        write_qword(0x8342D20, 0x18008000)
        write_qword(0x8342D28, 1)
        write(0x18008000, bytes(4096))

        widget = 0x18000000
        text_address = 0x18004000
        method_stub = 0x18006000
        return_address = 0x18007000
        write(widget, bytes(0x2000))
        write_qword(widget, vtable)
        write(text_address, payload + b"\0")
        write(method_stub, bytes(4096))
        write(return_address, bytes(4096))
        for method in (0x30, 0x40, 0x48, 0x58, 0x88):
            write_qword(vtable + method, method_stub)
        write(widget + 0x1FC, bytes([visible]))
        geometry = (78, 291, 484, 36) if kind == "LCD" else (60, 307, 520, 36)
        write(widget + 0x1E8, struct.pack("<4i", *geometry))
        write(widget + 0x214, struct.pack("<II", 0x0207A018, 0x0207A000))
        write(widget + 0x200, struct.pack("<I", 0xFFFFFFFF if use_cstr else 0x02FFFF61))
        write_qword(widget + 0x208, text_address if use_cstr else 0)
        write(widget + 0x520, struct.pack("<I", 0xFFFFFFFF))
        write(0x18010000, bytes(0x10000))

        cstr_calls = []
        rid_calls = []
        font_calls = []

        def c_string(address):
            result = bytearray()
            while len(result) < 160:
                value = bytes(emulator.mem_read(address + len(result), 1))
                if value == b"\0":
                    return bytes(result)
                result.extend(value)
            raise AssertionError("unterminated text")

        def intercept(uc, address, size, user):
            if address == 0x1769E30:
                cstr_calls.append((uc.reg_read(UC_ARM64_REG_X0),
                                   c_string(uc.reg_read(UC_ARM64_REG_X1))))
            elif address == 0x176BB40:
                rid_calls.append(uc.reg_read(UC_ARM64_REG_X1))
            elif address == 0x176B530:
                count = uc.reg_read(UC_ARM64_REG_X2)
                values = bytes(uc.mem_read(uc.reg_read(UC_ARM64_REG_X1), count * 4))
                font_calls.append(struct.unpack("<" + "I" * count, values))
            if 0x1769000 <= address < 0x176E000 or address == method_stub:
                uc.reg_write(UC_ARM64_REG_X0, 0)
                uc.reg_write(UC_ARM64_REG_PC, uc.reg_read(UC_ARM64_REG_LR))

        emulator.hook_add(UC_HOOK_CODE, intercept)
        emulator.reg_write(UC_ARM64_REG_X0, widget)
        emulator.reg_write(UC_ARM64_REG_SP, 0x18020000)
        emulator.reg_write(UC_ARM64_REG_LR, return_address)
        emulator.emu_start(0x19A78E8, return_address, count=100000)
        assert emulator.reg_read(UC_ARM64_REG_PC) == return_address
        if not visible:
            assert not cstr_calls and not rid_calls
        elif use_cstr:
            assert cstr_calls == [(widget + 0x7F8, payload)], cstr_calls
            assert not rid_calls
            assert len(font_calls) == 1 and font_calls[0][1:3] == (
                0x0207A018, 0x0207A000), font_calls
        else:
            assert not cstr_calls and rid_calls == [0x02FFFF61]
        print(f"{kind} bytes={len(payload)} visible={visible} cstr={use_cstr}: OK")

    for kind in ("LCD", "EVF"):
        run(kind, b"Pilot Ready (3/3)")
        run(kind, "Pilot 文字提示".encode("utf-8"))
        run(kind, b"x" * 159)
        run(kind, b"Hidden", visible=0)
        run(kind, b"RID", use_cstr=False)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("firmware", type=Path)
    verify(parser.parse_args().firmware)
