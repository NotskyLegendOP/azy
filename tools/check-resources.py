#!/usr/bin/env python3
"""Verifies the contents of a compiled Windows resource file (.res).

`zig rc` (used by the cross build on Linux) produces the binary resource format
rather than a COFF object, so the cross linker cannot embed it. The *content* of
resources/azy_skin.rc can still be verified: compile it with `zig rc` and assert
that the manifest, the icon and the version block are all present.

  python3 tools/check-resources.py /tmp/azy_skin.res

Exit code 1 (with a list of what is missing) when something is absent.
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

# RT_* resource types (winuser.h / winnt.h) and the ids the .rc uses.
RT_ICON = 3
RT_GROUP_ICON = 14
RT_VERSION = 16
RT_MANIFEST = 24

EXPECTED = [
    (RT_MANIFEST, 1, "application manifest (DPI awareness, comctl32 v6, asInvoker)"),
    (RT_ICON, None, "application icon"),
    (RT_GROUP_ICON, None, "icon group"),
    (RT_VERSION, None, "version information block"),
]


def read_name(data: bytes, offset: int) -> tuple[int, int]:
    """Reads a resource type/name field. Returns (id, offset after the field).

    A field is either a null-terminated UTF-16 string or the marker 0xFFFF
    followed by a 16-bit ordinal, which is how rc.exe has always written numeric
    ids into a .res file.
    """
    if data[offset : offset + 2] == b"\xff\xff":
        return struct.unpack_from("<H", data, offset + 2)[0], offset + 4
    end = offset
    while end + 1 < len(data) and data[end : end + 2] != b"\x00\x00":
        end += 2
    text = data[offset:end].decode("utf-16-le", errors="replace")
    try:
        return int(text), end + 2
    except ValueError:
        return -1, end + 2


def header_at(data: bytes, offset: int):
    """Reads a resource header at `offset`, or returns None when there is none."""
    if offset + 8 > len(data):
        return None
    data_size, header_size = struct.unpack_from("<II", data, offset)
    body = offset + 8
    if header_size < 16 or header_size > 4096 or offset + header_size + data_size > len(data):
        return None
    type_id, cursor = read_name(data, body)
    name_id, cursor = read_name(data, cursor)
    if not 0 < type_id <= 32 or name_id < 0:
        return None
    # The fixed fields start on a DWORD boundary.
    cursor += (-(cursor - body)) % 4
    if cursor + 8 > len(data):
        return None
    _version, _flags, language = struct.unpack_from("<IHH", data, cursor)
    # HeaderSize is measured from the start of the header.
    return (type_id, name_id, language, data_size, offset + header_size)


def parse(data: bytes) -> list[tuple[int, int, int, int]]:
    """Returns (type_id, name_id, language_id, data_size) for every resource.

    A .res file opens with a 32-byte null header (the "RES" magic) and then a
    sequence of length-prefixed resources. Resource data is padded so the next
    header lands on an aligned offset; the exact rule has changed between
    resource compiler versions, so the next header is searched for within a few
    bytes instead of assuming one.
    """
    entries = []
    offset = 32
    while True:
        found = None
        for candidate in range(offset, min(offset + 8, len(data))):
            header = header_at(data, candidate)
            if header is not None:
                found = (candidate, header)
                break
        if found is None:
            break
        _offset, (type_id, name_id, language, data_size, data_start) = found
        entries.append((type_id, name_id, language, data_size))
        offset = data_start + data_size + (-(data_start + data_size)) % 4
    return entries


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__.strip().splitlines()[0])
        print("usage: check-resources.py <file.res>")
        return 2

    path = Path(argv[1])
    entries = parse(path.read_bytes())
    print(f"{path}: {len(entries)} resource(s)")

    problems = []
    for type_id, name_of_entry, description in EXPECTED:
        matches = [e for e in entries if e[0] == type_id and (name_of_entry is None or e[1] == name_of_entry)]
        if not matches:
            problems.append(f"missing: {description} (RT {type_id})")
            continue
        total = sum(e[3] for e in matches)
        print(f"  ok  {description}: {len(matches)} entr(y/ies), {total} byte(s)")

    if problems:
        for problem in problems:
            print(f"  !!  {problem}")
        print("\nresources/azy_skin.rc did not compile into what it should.")
        return 1
    print("resource contents verified")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
