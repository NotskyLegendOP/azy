#!/usr/bin/env python3
"""Inspects a Windows executable: PE headers, subsystem and the resource tree.

Used to verify build artefacts without a Windows machine - in particular that the
application manifest, the icon and the version block really made it into the
executable, and that the resource directory is well formed (a malformed one is
invisible until Windows reads it).

  python3 tools/inspect-pe.py path/to/AzySkin.exe
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

SUBSYSTEMS = {1: "native", 2: "Windows GUI", 3: "Windows console"}
RESOURCE_KINDS = {1: "CURSOR", 2: "BITMAP", 3: "ICON", 4: "MENU", 5: "DIALOG", 6: "STRING",
                  9: "ACCELERATOR", 10: "RCDATA", 12: "GROUP_CURSOR", 14: "GROUP_ICON",
                  16: "VERSIONINFO", 24: "MANIFEST"}
RT_MANIFEST = 24


class Pe:
    def __init__(self, data: bytes) -> None:
        self.data = data
        if data[:2] != b"MZ":
            raise ValueError("not an MZ executable")
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe : pe + 4] != b"PE\0\0":
            raise ValueError("no PE signature")
        self.pe = pe
        (self.machine, self.section_count, _stamp, _sym, _nsym, self.optional_size, self.characteristics) = (
            struct.unpack_from("<HHIIIHH", data, pe + 4)
        )
        optional = pe + 24
        self.magic = struct.unpack_from("<H", data, optional)[0]
        plus = self.magic == 0x20B
        self.subsystem = struct.unpack_from("<H", data, optional + (68 if plus else 68))[0]
        directory_count = struct.unpack_from("<I", data, optional + (108 if plus else 92))[0]
        directories_at = optional + (112 if plus else 96)
        self.directories = [
            struct.unpack_from("<II", data, directories_at + 8 * i) for i in range(min(directory_count, 16))
        ]
        self.sections = []
        table = pe + 24 + self.optional_size
        for i in range(self.section_count):
            offset = table + 40 * i
            name = data[offset : offset + 8].rstrip(b"\0").decode("ascii", "replace")
            virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from("<IIII", data, offset + 8)
            self.sections.append((name, virtual_address, virtual_size, raw_pointer, raw_size))

    def rva_to_offset(self, rva: int) -> int | None:
        for _name, va, vsize, raw, raw_size in self.sections:
            if va <= rva < va + max(vsize, raw_size):
                return raw + (rva - va)
        return None

    def read_rva(self, rva: int, size: int) -> bytes:
        offset = self.rva_to_offset(rva)
        if offset is None:
            raise ValueError(f"rva {rva:#x} is not inside any section")
        return self.data[offset : offset + size]

    def resources(self) -> list[tuple[int | str, int | str, int, int, int]]:
        """Returns (type, name, language, size, rva) for every resource.

        Offsets inside the resource tree are relative to the start of the resource
        *section* (an RVA), not to the file, so the tree is read through the
        section's file offset.
        """
        if len(self.directories) < 3 or self.directories[2][0] == 0:
            return []
        section_rva = self.directories[2][0]
        base = self.rva_to_offset(section_rva)
        if base is None:
            return []
        found: list[tuple[int | str, int | str, int, int, int]] = []

        def read_name_field(offset: int) -> int | str:
            first = struct.unpack_from("<I", self.data, base + offset)[0]
            if first & 0x80000000:
                string_at = base + (first & 0x7FFFFFFF)
                length = struct.unpack_from("<H", self.data, string_at)[0]
                return self.data[string_at + 2 : string_at + 2 + 2 * length].decode("utf-16-le", "replace")
            return first & 0xFFFF

        def walk(directory_offset: int, level: int, path: list) -> None:
            at = base + directory_offset
            named, numbered = struct.unpack_from("<HH", self.data, at + 12)
            for index in range(named + numbered):
                entry = at + 16 + 8 * index
                key = read_name_field(entry - base)
                target = struct.unpack_from("<I", self.data, entry + 4)[0]
                if target & 0x80000000:
                    if level >= 2:
                        continue
                    walk(target & 0x7FFFFFFF, level + 1, path + [key])
                else:
                    # IMAGE_RESOURCE_DATA_ENTRY: OffsetToData first, then Size. The
                    # offset is relative to the start of the resource directory, so
                    # the payload's RVA is section_rva + offset.
                    offset_within, size = struct.unpack_from("<II", self.data, base + target)
                    data_rva = section_rva + offset_within
                    language = key if level >= 2 else 0
                    found.append((path[0] if path else 0, path[1] if len(path) > 1 else 0,
                                  language, size, data_rva))

        walk(0, 0, [])
        return found


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print("usage: inspect-pe.py <executable>")
        return 2
    path = Path(argv[1])
    data = path.read_bytes()
    try:
        pe = Pe(data)
    except ValueError as error:
        print(f"{path}: {error}")
        return 1

    print(f"{path} ({len(data):,} bytes)")
    # A budget, not a target: the brief asks for something light, and the docs
    # quote the size, so a number that drifts silently is a documentation bug. It
    # also catches the one thing that would blow the budget instantly - pulling in
    # a UI framework or a static runtime.
    budget = 1_000_000
    over_budget = len(data) > budget
    print(f"  size budget        {budget:,} bytes "
          f"({'OK' if not over_budget else 'EXCEEDED'}, {100.0 * len(data) / budget:.1f}% used)")
    print(f"  machine            0x{pe.machine:04x} ({'x64' if pe.machine == 0x8664 else 'other'})")
    print(f"  magic              0x{pe.magic:04x} ({'PE32+' if pe.magic == 0x20B else 'PE32'})")
    print(f"  subsystem          {pe.subsystem} ({SUBSYSTEMS.get(pe.subsystem, '?')})")
    print(f"  sections           {', '.join(s[0] for s in pe.sections)}")

    resources = pe.resources()
    if not resources:
        print("  resources          none (the application falls back to its runtime DPI call)")
        return 1 if over_budget else 0

    print(f"  resources          {len(resources)}")
    manifest_ok = False
    for type_id, name, language, size, rva in sorted(resources, key=lambda r: (str(r[0]), str(r[1]))):
        kind = RESOURCE_KINDS.get(type_id, str(type_id)) if isinstance(type_id, int) else type_id
        print(f"    {kind:<12} name={name:<6} language=0x{language:04x} {size:>7,} bytes")
        if type_id == RT_MANIFEST and name == 1:
            text = pe.read_rva(rva, size).decode("utf-8", "replace")
            has_dpi = "permonitorv2" in text or "PerMonitorV2" in text or "permonitor" in text
            has_controls = "Microsoft.Windows.Common-Controls" in text
            manifest_ok = has_dpi and has_controls
            print(f"      manifest: {len(text)} chars, DPI awareness={has_dpi}, comctl32 v6={has_controls}")

    if not manifest_ok:
        print("  !! the application manifest is missing or does not declare DPI awareness and comctl32 v6")
        return 1
    return 1 if over_budget else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
