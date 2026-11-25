#!/usr/bin/env python3
"""Check the A7C II menu-UI constants against a stock appFw.so image.

The module patches a running appFw: menu record fields, vtable slots, two code
sites and a set of GOT entries.  Every one of those is a hardcoded offset, so a
single wrong constant turns into a patched-but-wrong camera instead of a failed
load.  This script treats the stock image as the reference and refuses to run if
any address disagrees with it.

What is checked:

* image layout: program headers, sections, VA/file-offset delta
* the page descriptor the owned rows are cloned from (fields and zero slots)
* the code the two page hooks replace, and the trampoline prologue
* every pointer slot the install writes, through its R_AARCH64_RELATIVE addend
* the two tables the list transport validates before it installs
* the key records and the dispatch prologue of the native list screen

It does not execute anything: this is static evidence, the on-camera behaviour
still has to be verified on the body.

Two ILCE-7CM2 2.01 sources are accepted, both described by the patch map in
Pilot/patch.a7c2.json (target.accepted_sources):

    stock  oldProjects/FlashDir/backup1.3/original/appFw.so.bin
    fl23   oldProjects/FlashDir/backup1.3/candidate/appFw.so, the same image
           after the FL2/FL3 Creative Look port, whose eight in-place writes
           stay clear of every address checked here

The source is identified by its whole-image SHA-256, so an unrecorded variant is
reported instead of silently passing.  --allow-unknown-source inspects such an
image anyway, in which case only the checked sites are proven to agree.

Usage:
    python verify_a7c2_offsets.py [--image path/to/appFw.so.bin] [--verbose]
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

try:
    from elftools.elf.elffile import ELFFile
    from elftools.elf.relocation import RelocationSection
except ImportError:  # pragma: no cover - tooling guidance, not a test result
    sys.exit("pyelftools is required: pip install pyelftools")

MODULE_ROOT = Path(__file__).resolve().parent.parent
HEADER = MODULE_ROOT / "a7c2/pilot_a7c2_menu.h"
DEFAULT_IMAGE = MODULE_ROOT.parents[2] / "oldProjects/FlashDir/backup1.3/original/appFw.so.bin"
PATCH_MAP = MODULE_ROOT.parents[1] / "patch.a7c2.json"

# The accepted A7C II image sources live with the installer so both sides read
# the same hashes; importing them keeps this script free of a second table.
sys.path.insert(0, str(MODULE_ROOT.parents[1] / "patch"))
from on_camera_profiles import classify_digest, describe  # noqa: E402

# ILCE-7CM2 2.01 appFw.so.  Recorded from the update payload and re-derived by
# the source-rebuild work in a7c2-re/report/2026-11-05-appfw-source-rebuild.
IMAGE_SIZE = 99_960_744
ENTRY = 0x1660680
LOAD0_FILESZ = 0x51A6CFF
LOAD1_OFFSET = 0x51A7718
LOAD1_VADDR = 0x51B7718
LOAD1_FILESZ = 0xDAC8A0
LOAD1_MEMSZ = 0x46EA5B8
DYNAMIC_OFFSET = 0x5B3BB60  # file offset
DYNAMIC_VADDR = 0x5B4BB60
RELRO_VADDR = 0x51B7718
RELRO_SIZE = 0xA098E8
VA_DELTA = 0x10000  # mapped VA = file offset + VA_DELTA for PT_LOAD[1]

SECTIONS = {
    ".text": (0x1660680, 0x20A3B20),
    ".rodata": (0x37041B0, 0x1A9C834),
    ".init_array": (0x51B7718, None),
    ".data.rel.ro": (0x51B82A0, 0x9938C0),
    ".got": (0x5B4BE70, 0x75178),
    ".got.plt": (0x5BC0FE8, 0x1B70),
    ".data": (0x5BC2B58, 0x3A1460),
    ".bss": (0x5F63FB8, 0x393DD18),
}

# .data.rel.ro record the owned rows are cloned from (screen 0x220 "info").
INFO_RECORD = 0x5A21D80
INFO_RECORD_WORDS = {
    0x00: 0x021C6542,  # title resource id
    0x14: 0xB81,
    0x18: 0x20000001,
    0x1C: 0x20000001,
    0x20: 0x220,  # screen
    0x24: 0x4B7,
}
INFO_RECORD_ZERO_SLOTS = (0x78, 0x80, 0x98, 0xA8, 0xB0)

# Code the install replaces (the low 26 bits are a BL immediate).
CODE_SITES = {
    0x2001EB4: (0x9421E3D5, "page value call"),
    0x1E0F990: (0x94155C0C, "page format call"),
    0x1B6DCD8: (0x97FFE42D, "alias call"),
    0x1B6DE94: (0x97FFE451, "reverse call"),
}
INFO_TITLE_GETTER = 0x1BF20E0
INFO_TITLE_GETTER_WORDS = (0x52800020, 0xD65F03C0)  # mov w0,#1 ; ret

# Pointer slots written by the install.  The expected value is the addend of a
# R_AARCH64_RELATIVE relocation, i.e. the address once appFw is loaded at base.
POINTER_SLOTS = {
    0x5204410: (0x1E0F6E4, "presenter factory"),
    0x5B13598: (0x217CBD8, "root enter"),
    0x5BC1FE8: (0x165CFD0, "TextSprite::setString(uint32_t)"),
    0x597D578 + 0x18: (0x1B6EA38, "item normal record"),
    0x597D578 + 0x730: (0x1B66D8C, "item alias"),
    0x597D578 + 0x738: (0x1B66FD8, "item reverse"),
    0x597D578 + 0x7B0: (0x1B5AC70, "item tree builder"),
    0x5B08C00 + 0x10: (0x1E0F8D4, "presenter update"),
    0x5B08C00 + 0x28: (0x1C772A0, "presenter rebuild"),
    # native list transport (screen 0x2cb)
    0x597D850: (0x1B5EA88, "list children"),
    0x597D620: (0x1B598B0, "list row value"),
    0x5208438: (0x1CF7840, "list controller factory"),
    0x5208430: (0x1D1E1D8, "list presenter factory"),
    0x5B13600: (0x2181A38, "list enter"),
    0x5B135E0: (0x21824B4, "list controller destructor"),
    0x5AFAEA0: (0x1C335D0, "list presenter destructor"),
    0x5AFAEB0: (0x1D1E3C8, "list refresh"),
    0x5B0C498: (0x1E77DD8, "list open"),
    0x5B0C4A0: (0x1CA4C0C, "list close"),
}
# The stock app leaves these empty; the install asserts that before writing.
POINTER_SLOTS_ZERO = {
    0x5204420: "presenter factory tail",
    0x5921EF8: "controller member tail",
}

LIST_DISPATCH = 0x1934974
LIST_DISPATCH_WORDS = (0xA9BC7BFD, 0x910003FD, 0xA90153F3)  # stp ; mov ; stp
LIST_WRAPPER_CALLS = {0x1B6BEC4: 0x1B5EA88, 0x1B6AE28: 0x1B598B0, 0x1D1E448: 0x2182670}
LIST_CLASS_TABLE = 0x3992A38
LIST_CLASS_TABLE_WORDS = {
    0x00: 0x00020004,
    0x04: 0x00060005,
    0x08: 0x00070002,
    0x18: 0x0002000A,
    0x1C: 0x000C000B,
}
KEY_TABLE = 0x5BC2B60
KEY_TABLE_RECORDS = (
    (2, 0x02010000, 0x02010002, 0x02010001),
    (6, 0x02010003, 0x02010005, 0x02010004),
    (4, 0x02010006, 0x02010008, 0x02010007),
    (0x16, 0x02010009, 0x0201000B, 0x0201000A),
    (0x14, 0x0201000C, 0x0201000E, 0x0201000D),
    (0x13, 0x0201000F, 0x20000001, 0x02010010),
)


class Checker:
    def __init__(self, verbose: bool) -> None:
        self.verbose = verbose
        self.failures: list[str] = []
        self.passed = 0

    def check(self, ok: bool, label: str, detail: str = "") -> None:
        if ok:
            self.passed += 1
            if self.verbose:
                print(f"  ok    {label}" + (f"  ({detail})" if detail else ""))
            return
        self.failures.append(f"{label}" + (f": {detail}" if detail else ""))
        print(f"  FAIL  {label}" + (f"  ({detail})" if detail else ""))

    def equal(self, actual, expected, label: str) -> None:
        self.check(actual == expected, label, f"{actual:#x} != {expected:#x}"
                   if isinstance(actual, int) and isinstance(expected, int)
                   else f"{actual!r} != {expected!r}")


class Image:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.data = path.read_bytes()
        self.stream = path.open("rb")
        self.elf = ELFFile(self.stream)
        self.relocations = self._relocations()

    def _relocations(self) -> dict[int, tuple[int, int]]:
        table: dict[int, tuple[int, int]] = {}
        for section in self.elf.iter_sections():
            if isinstance(section, RelocationSection):
                for entry in section.iter_relocations():
                    table[entry["r_offset"]] = (
                        entry["r_info_type"],
                        entry["r_addend"] if entry.is_RELA() else 0,
                    )
        return table

    def offset(self, va: int) -> int:
        for segment in self.elf.iter_segments():
            if segment["p_type"] != "PT_LOAD":
                continue
            start = segment["p_vaddr"]
            size = segment["p_filesz"]
            if start <= va < start + size:
                return segment["p_offset"] + (va - start)
        raise KeyError(f"VA {va:#x} is not backed by the file")

    def u32(self, va: int) -> int:
        return struct.unpack_from("<I", self.data, self.offset(va))[0]

    def u64(self, va: int) -> int:
        return struct.unpack_from("<Q", self.data, self.offset(va))[0]

    def pointer(self, va: int) -> tuple[int, str]:
        """Value of a pointer slot after relocation, plus how it was found."""
        reloc = self.relocations.get(va)
        if reloc is not None and reloc[0] == 1027:  # R_AARCH64_RELATIVE
            return reloc[1], "reloc"
        return self.u64(va), "raw"

    def close(self) -> None:
        self.stream.close()


def parse_header(path: Path) -> dict[str, int]:
    constants: dict[str, int] = {}
    pattern = re.compile(r"^#define\s+([A-Z0-9_]+)\s+0x([0-9a-fA-F]+)u?l?$", re.M)
    for name, value in pattern.findall(path.read_text(encoding="utf-8")):
        constants[name] = int(value, 16)
    return constants


def branch_target(instruction: int, pc: int) -> int:
    if instruction & 0xFC000000 != 0x94000000:
        raise ValueError(f"{instruction:#x} at {pc:#x} is not a BL")
    immediate = instruction & 0x03FFFFFF
    if immediate & 0x02000000:
        immediate -= 0x04000000
    return pc + immediate * 4


def check_layout(chk: Checker, image: Image) -> None:
    print("image layout")
    chk.equal(len(image.data), IMAGE_SIZE, "file size")
    chk.equal(image.elf.header["e_entry"], ENTRY, "entry point")

    loads = [seg for seg in image.elf.iter_segments() if seg["p_type"] == "PT_LOAD"]
    chk.equal(len(loads), 2, "PT_LOAD count")
    chk.equal(loads[0]["p_filesz"], LOAD0_FILESZ, "PT_LOAD[0] file size")
    chk.equal(loads[1]["p_offset"], LOAD1_OFFSET, "PT_LOAD[1] file offset")
    chk.equal(loads[1]["p_vaddr"], LOAD1_VADDR, "PT_LOAD[1] vaddr")
    chk.equal(loads[1]["p_filesz"], LOAD1_FILESZ, "PT_LOAD[1] file size")
    chk.equal(loads[1]["p_memsz"], LOAD1_MEMSZ, "PT_LOAD[1] memory size")
    chk.equal(loads[1]["p_vaddr"] - loads[1]["p_offset"], VA_DELTA, "VA/file delta")

    dynamic = next(seg for seg in image.elf.iter_segments() if seg["p_type"] == "PT_DYNAMIC")
    chk.equal(dynamic["p_offset"], DYNAMIC_OFFSET, "PT_DYNAMIC file offset")
    chk.equal(dynamic["p_vaddr"], DYNAMIC_VADDR, "PT_DYNAMIC vaddr")
    relro = next(seg for seg in image.elf.iter_segments() if seg["p_type"] == "PT_GNU_RELRO")
    chk.equal(relro["p_vaddr"], RELRO_VADDR, "PT_GNU_RELRO vaddr")
    chk.equal(relro["p_memsz"], RELRO_SIZE, "PT_GNU_RELRO size")

    for name, (vaddr, size) in SECTIONS.items():
        section = image.elf.get_section_by_name(name)
        if section is None:
            chk.check(False, f"section {name}", "missing")
            continue
        chk.equal(section["sh_addr"], vaddr, f"section {name} addr")
        if size is not None:
            chk.equal(section["sh_size"], size, f"section {name} size")


def check_info_record(chk: Checker, image: Image) -> None:
    print("info page descriptor (owned row template)")
    for offset, expected in INFO_RECORD_WORDS.items():
        chk.equal(image.u32(INFO_RECORD + offset), expected, f"record+{offset:#x}")
    for offset in INFO_RECORD_ZERO_SLOTS:
        chk.equal(image.pointer(INFO_RECORD + offset)[0], 0, f"record+{offset:#x} callback")


    reference, how = image.pointer(INFO_RECORD + 0x70)
    chk.check(reference == INFO_TITLE_GETTER, "record+0x70 title getter",
              f"{how} -> {reference:#x}")


def check_code(chk: Checker, image: Image) -> None:
    print("hooked code")
    for va, (word, label) in CODE_SITES.items():
        chk.equal(image.u32(va), word, label)
    chk.equal(image.u32(INFO_TITLE_GETTER), INFO_TITLE_GETTER_WORDS[0], "title getter mov")
    chk.equal(image.u32(INFO_TITLE_GETTER + 4), INFO_TITLE_GETTER_WORDS[1], "title getter ret")
    for index, word in enumerate(LIST_DISPATCH_WORDS):
        chk.equal(image.u32(LIST_DISPATCH + 4 * index), word, f"list dispatch +{index * 4:#x}")
    for va, target in LIST_WRAPPER_CALLS.items():
        chk.equal(branch_target(image.u32(va), va), target, f"wrapper call at {va:#x}")


def check_pointers(chk: Checker, image: Image) -> None:
    print("pointer slots")
    for va, (expected, label) in POINTER_SLOTS.items():
        value, how = image.pointer(va)
        chk.check(value == expected, f"{label} @{va:#x}", f"{how} -> {value:#x}")
    for va, label in POINTER_SLOTS_ZERO.items():
        value, how = image.pointer(va)
        chk.check(value == 0, f"{label} @{va:#x} empty", f"{how} -> {value:#x}")


def check_tables(chk: Checker, image: Image) -> None:
    print("list transport tables")
    for offset, word in LIST_CLASS_TABLE_WORDS.items():
        chk.equal(image.u32(LIST_CLASS_TABLE + offset), word, f"class table +{offset:#x}")
    for record, words in enumerate(KEY_TABLE_RECORDS):
        for index, word in enumerate(words):
            chk.equal(image.u32(KEY_TABLE + 16 * record + 4 * index), word,
                      f"key record {record} word {index}")


def check_header(chk: Checker, image: Image, constants: dict[str, int]) -> None:
    """Bind the constants the module actually uses to the checks above."""
    print("module constants")
    expected = {
        "INFO_RECORD": INFO_RECORD,
        "INFO_SCREEN": 0x220,
        "NEW_ITEM": 0x601,
        "NEW_TITLE": 0x02FEFFF0,
        "PILOT_LIST_SCREEN": 0x2CB,
        "TEXT_UINT_SLOT": 0x5BC1FE8,
        "FACTORY_SLOT": 0x5204410,
        "ROOT_ENTER_SLOT": 0x5B13598,
        "PAGE_VALUE_CALL": 0x2001EB4,
        "PAGE_FORMAT_CALL": 0x1E0F990,
        "ALIAS_CALL": 0x1B6DCD8,
        "REVERSE_CALL": 0x1B6DE94,
        "PRESENTER_VTABLE": 0x5B08C00,
        "ITEM_UTIL_VTABLE": 0x597D578,
        "F_INFO_TITLE_GETTER": INFO_TITLE_GETTER,
        "F_NEW_NORMAL": 0x597EE10,
        "NEW_ALIAS": 0x602,
        "NEW_REVERSE": 0xB81,
    }
    for name, value in expected.items():
        if name not in constants:
            chk.check(False, f"header define {name}", "missing")
            continue
        chk.equal(constants[name], value, f"header define {name}")

    # The header claims every address is validated; the record and the code site
    # must actually be file backed, otherwise the check above proves nothing.
    for name in ("INFO_RECORD", "TEXT_UINT_SLOT", "PRESENTER_VTABLE", "ITEM_UTIL_VTABLE"):
        if name not in constants:
            continue
        try:
            image.offset(constants[name])
        except KeyError as error:
            chk.check(False, f"header define {name} is file backed", str(error))


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def report_source(image_path: Path, allow_unknown: bool) -> bool:
    """Identify the image against the recorded sources; return False to stop."""
    if not PATCH_MAP.is_file():
        print(f"source: profile check skipped ({PATCH_MAP.name} not found)")
        return True
    patch_map = json.loads(PATCH_MAP.read_text(encoding="utf-8-sig"))
    digest = sha256_file(image_path)
    state, profile = classify_digest(digest, image_path.stat().st_size, patch_map)
    print(f"sha256 {digest}")
    if state == "baseline":
        print(f"source: profile={profile} (recorded, unpatched)")
        return True
    if state == "patched":
        print(f"source: profile={profile} already carries the Pilot AP patch",
              file=sys.stderr)
        print("this dump is not a source image; restore the AP and dump again",
              file=sys.stderr)
        return False
    print(f"source: not a recorded ILCE-7CM2 2.01 image ({describe(patch_map)})",
          file=sys.stderr)
    if allow_unknown:
        print("source: --allow-unknown-source, checking the sites anyway")
        return True
    print("pass --allow-unknown-source to check the constants anyway",
          file=sys.stderr)
    return False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--image", type=Path, default=DEFAULT_IMAGE,
                        help="ILCE-7CM2 2.01 appFw.so dump (stock or FL23)")
    parser.add_argument("--allow-unknown-source", action="store_true",
                        help="check the sites even when the image hash is unrecorded")
    parser.add_argument("--verbose", action="store_true", help="print passing checks too")
    args = parser.parse_args()

    image_path = args.image.expanduser()
    if not image_path.is_file():
        print(f"image not found: {image_path}", file=sys.stderr)
        print("pass --image to point at an ILCE-7CM2 2.01 appFw.so dump", file=sys.stderr)
        return 2

    chk = Checker(args.verbose)
    print(f"image  {image_path}  ({image_path.stat().st_size} bytes)")
    print(f"header {HEADER.relative_to(MODULE_ROOT)}")
    if not report_source(image_path, args.allow_unknown_source):
        return 1

    constants = parse_header(HEADER)
    if not constants:
        print("no constants parsed from the header", file=sys.stderr)
        return 2

    image = Image(image_path)
    try:
        check_layout(chk, image)
        check_info_record(chk, image)
        check_code(chk, image)
        check_pointers(chk, image)
        check_tables(chk, image)
        check_header(chk, image, constants)
    finally:
        image.close()

    print()
    if chk.failures:
        print(f"{len(chk.failures)} failure(s), {chk.passed} passed")
        for failure in chk.failures:
            print(f"  - {failure}")
        return 1
    print(f"all {chk.passed} checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
