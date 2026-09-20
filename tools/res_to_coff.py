#!/usr/bin/env python3
"""Converts a compiled Windows resource file (.res) into a COFF object file.

Why this exists: `zig rc` (resinator) - the resource compiler available in the
Linux cross build - writes the binary resource format that `rc.exe` uses as its
intermediate file. lld-link cannot consume that: it expects the resource tree in
two COFF sections, `.rsrc$01` (directories, entries and names) and `.rsrc$02`
(the resource payloads), which is what MSVC's cvtres produces. This script does
that conversion, so the cross-compiled executable embeds the same manifest, icon
and version block as the MSVC build - which also means the resource path is
verified on every CI run instead of only on Windows.

Layout follows the PE specification: the section starts with the root resource
directory, subdirectory offsets have their high bit set, named entries store a
length-prefixed UTF-16 string, and every data entry holds a section-relative RVA.

  python3 tools/res_to_coff.py in.res out.obj
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

IMAGE_SCN_CNT_INITIALIZED_DATA = 0x00000040
IMAGE_SCN_MEM_DISCARDABLE = 0x02000000
IMAGE_SCN_MEM_READ = 0x40000000
IMAGE_SCN_ALIGN_4BYTES = 0x00300000
FILE_MACHINE_AMD64 = 0x8664


# --------------------------------------------------------------------------- #
# Reading the .res file
# --------------------------------------------------------------------------- #
def read_name(data: bytes, offset: int) -> tuple[int | str, int]:
    """Reads a resource type/name field: 0xFFFF + ordinal, or a UTF-16 string."""
    if data[offset : offset + 2] == b"\xff\xff":
        return struct.unpack_from("<H", data, offset + 2)[0], offset + 4
    end = offset
    while end + 1 < len(data) and data[end : end + 2] != b"\x00\x00":
        end += 2
    text = data[offset:end].decode("utf-16-le", errors="replace")
    return text, end + 2


def header_at(data: bytes, offset: int):
    if offset + 8 > len(data):
        return None
    size, header_size = struct.unpack_from("<II", data, offset)
    body = offset + 8
    if header_size < 16 or header_size > 4096 or offset + header_size + size > len(data):
        return None
    type_id, cursor = read_name(data, body)
    name_id, cursor = read_name(data, cursor)
    cursor += (-(cursor - body)) % 4
    if cursor + 16 > len(data):
        return None
    language = struct.unpack_from("<H", data, cursor + 6)[0]
    codepage = struct.unpack_from("<I", data, cursor + 8)[0]
    # HeaderSize is measured from the start of the header, so the payload begins
    # header_start + HeaderSize (not +8+HeaderSize).
    return type_id, name_id, language, codepage, offset + header_size, size


def parse_res(data: bytes) -> list[tuple[int | str, int | str, int, int, bytes]]:
    """Returns (type, name, language, codepage, payload) for every resource.

    The 32-byte null header at the start of the file is skipped; the next header
    is searched for within a few bytes because the padding rule between resources
    differs between resource compiler versions.
    """
    entries = []
    offset = 32
    while True:
        found = None
        for candidate in range(offset, min(offset + 16, len(data))):
            header = header_at(data, candidate)
            if header is not None:
                found = header
                break
        if found is None:
            break
        type_id, name_id, language, codepage, data_start, size = found
        entries.append((type_id, name_id, language, codepage, data[data_start : data_start + size]))
        offset = data_start + size + (-(data_start + size)) % 4  # data is DWORD aligned
    return entries


# --------------------------------------------------------------------------- #
# Building the resource tree
# --------------------------------------------------------------------------- #
class Section:
    """A byte buffer that hands out offsets, then gets patched in place."""

    def __init__(self) -> None:
        self.data = bytearray()

    def alloc(self, size: int) -> int:
        offset = len(self.data)
        self.data.extend(b"\x00" * size)
        return offset

    def align(self, boundary: int = 4) -> None:
        self.data.extend(b"\x00" * ((-len(self.data)) % boundary))


def build_tree(entries: list[tuple[int | str, int | str, int, int, bytes]]):
    """Builds (.rsrc$01, .rsrc$02) exactly as cvtres lays them out."""
    tree: dict = {}
    for type_id, name_id, language, codepage, payload in entries:
        tree.setdefault(type_id, {}).setdefault(name_id, {})[language] = (codepage, payload)

    section1 = Section()
    payloads = bytearray()
    pending: list[tuple[int, int, bytes]] = []  # (data-entry offset, codepage, payload)

    def emit_name(value: str) -> int:
        offset = section1.alloc(2 + 2 * len(value) + 2)
        struct.pack_into("<H", section1.data, offset, len(value))
        section1.data[offset + 2 : offset + 2 + 2 * len(value)] = value.encode("utf-16-le")
        section1.align(2)
        return offset

    def emit_directory(items: list) -> int:
        """items: list of (key, value); value is a dict (subdirectory) or a
        (codepage, payload) tuple (leaf)."""
        directory = section1.alloc(16)
        named = [(k, v) for k, v in items if isinstance(k, str)]
        numbered = [(k, v) for k, v in items if not isinstance(k, str)]
        entries_offset = section1.alloc(8 * (len(named) + len(numbered)))

        struct.pack_into("<II", section1.data, directory, 0, 0)  # characteristics, timestamp
        struct.pack_into("<HH", section1.data, directory + 12, len(named), len(numbered))

        for index, (key, value) in enumerate(named + numbered):
            entry = entries_offset + 8 * index
            if isinstance(key, str):
                name_offset = emit_name(key)
                struct.pack_into("<I", section1.data, entry, 0x80000000 | name_offset)
            else:
                struct.pack_into("<I", section1.data, entry, key)
            if isinstance(value, dict):
                subdirectory = emit_directory(list(value.items()))
                struct.pack_into("<I", section1.data, entry + 4, 0x80000000 | subdirectory)
            else:
                codepage, payload = value
                data_entry = section1.alloc(16)
                struct.pack_into("<I", section1.data, entry + 4, data_entry)
                pending.append((data_entry, codepage, payload))
        return directory

    emit_directory(list(tree.items()))
    section1.align(4)

    # Payloads live in the second section, so their RVAs are offset by the size of
    # the first one - the offsets inside the tree are relative to the start of the
    # finished .rsrc section.
    base = len(section1.data)
    for data_entry, codepage, payload in pending:
        payloads.extend(b"\x00" * ((-len(payloads)) % 4))
        offset = base + len(payloads)
        struct.pack_into("<IIII", section1.data, data_entry, offset, len(payload), codepage, 0)
        payloads.extend(payload)

    payloads.extend(b"\x00" * ((-len(payloads)) % 4))
    return bytes(section1.data), bytes(payloads)


# --------------------------------------------------------------------------- #
# Writing the COFF object
# --------------------------------------------------------------------------- #
def section_header(name: str, size: int, raw_offset: int) -> bytes:
    return struct.pack(
        "<8sIIIIIIHHI",
        name.encode("ascii")[:8],
        size,  # VirtualSize
        0,  # VirtualAddress
        size,  # SizeOfRawData
        raw_offset,  # PointerToRawData
        0,  # PointerToRelocations
        0,  # PointerToLinenumbers
        0,  # NumberOfRelocations
        0,  # NumberOfLinenumbers
        IMAGE_SCN_CNT_INITIALIZED_DATA
        | IMAGE_SCN_MEM_DISCARDABLE
        | IMAGE_SCN_MEM_READ
        | IMAGE_SCN_ALIGN_4BYTES,
    )


def build_object(section1: bytes, section2: bytes) -> bytes:
    headers_size = 20 + 40 * 2
    first_raw = headers_size
    second_raw = first_raw + len(section1)

    coff_header = struct.pack(
        "<HHIIIHH",
        FILE_MACHINE_AMD64,
        2,  # NumberOfSections
        0,  # TimeDateStamp
        0,  # PointerToSymbolTable
        0,  # NumberOfSymbols
        0,  # SizeOfOptionalHeader
        0,  # Characteristics
    )
    out = bytearray(coff_header)
    out += section_header(".rsrc$01", len(section1), first_raw)
    out += section_header(".rsrc$02", len(section2), second_raw)
    assert len(out) == headers_size
    out += section1
    out += section2
    return bytes(out)


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print("usage: res_to_coff.py <in.res> <out.obj>")
        return 2
    entries = parse_res(Path(argv[1]).read_bytes())
    if not entries:
        print(f"no resources found in {argv[1]}")
        return 1
    section1, section2 = build_tree(entries)
    Path(argv[2]).write_bytes(build_object(section1, section2))
    types = {}
    for type_id, _name, _lang, _codepage, payload in entries:
        types[type_id] = types.get(type_id, 0) + 1
    kind = {3: "ICON", 14: "GROUP_ICON", 16: "VERSIONINFO", 24: "MANIFEST"}
    summary = ", ".join(f"{kind.get(t, t)} x{c}" for t, c in sorted(types.items(), key=lambda kv: str(kv[0])))
    print(f"{argv[2]}: {len(entries)} resource(s) [{summary}], {len(section1) + len(section2)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
