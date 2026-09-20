#!/usr/bin/env python3
"""Checks the overlay's contracts, which no compiler checks for Azy.

The duplicate window's look is decided by two files that must agree exactly:
resources/shaders/gloss.hlsl (the shader and its constant buffer) and
src/win32/gloss/gloss_overlay.cpp (the AzyParams struct the CPU writes). Nothing
in either compiler compares them - a mismatch shows up only as wrong pixels on a
user's machine - so this script does the comparison:

  * the region slot counts match the header's constants,
  * the constant buffer's member list matches the struct's, in order and in type,
  * the constant buffer is written in the packing-safe style (no float3),
  * the C++ static_assert agrees with the size the HLSL packing rules produce,
  * nothing anywhere calls the monitor form of the capture API (a monitor capture
    would include the duplicate window itself: an infinite mirror), and
  * the capture still refuses to mirror Azy's own process.

Run: python3 tools/check-overlay.py [repo root]
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

FAILURES: list[str] = []


def fail(message: str) -> None:
    FAILURES.append(message)


def read(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except OSError as exc:  # pragma: no cover - only fires on a broken checkout
        fail(f"{path}: {exc}")
        return ""


def block(text: str, start_pattern: str, end: str = "};") -> str:
    start = re.search(start_pattern, text)
    if not start:
        fail(f"could not find {start_pattern!r}")
        return ""
    end_index = text.find(end, start.end())
    if end_index < 0:
        fail(f"could not find the end of {start_pattern!r}")
        return ""
    return text[start.end() : end_index]


# HLSL scalar/vector types -> (component count, byte size).
HLSL_TYPES = {
    "float": (1, 4),
    "float2": (2, 8),
    "float3": (3, 12),
    "float4": (4, 16),
    "int": (1, 4),
    "uint": (1, 4),
}


def hlsl_members(source: str, macros: dict[str, int]) -> list[tuple[str, str, int]]:
    """(name, type, element count) for every member of the cbuffer, in order.

    `macros` resolves array dimensions written as a #define (PASS_SLOTS).
    """
    body = block(source, r"cbuffer\s+AzyParams\s*:\s*register\(b0\)\s*\{")
    members: list[tuple[str, str, int]] = []
    for line in body.splitlines():
        line = line.split("//")[0].strip()
        if not line or line.startswith("{"):
            continue
        match = re.match(r"^(float[234]?|int|uint)\s+(\w+)(\[(\w+)\])?\s*;$", line)
        if not match:
            if line:
                fail(f"gloss.hlsl: unrecognised cbuffer member {line!r}")
            continue
        kind, name, _, array_size = match.groups()
        if array_size is None:
            count = 1
        elif array_size.isdigit():
            count = int(array_size)
        else:
            count = macros.get(array_size, -1000)
        members.append((name, kind, count))
    return members


def hlsl_layout(members: list[tuple[str, str, int]]) -> int:
    """Byte size of the cbuffer under HLSL's packing rules.

    The rules that matter here: members are laid out in order on 4-byte boundaries,
    a member never straddles a 16-byte register, and an array element always starts
    in its own register. These are the rules the CPU mirror has to reproduce by
    hand, which is the whole reason this function exists.
    """
    offset = 0
    for _, kind, count in members:
        components, size = HLSL_TYPES[kind]
        if count > 1:
            # Array: 16-byte aligned, one register per element.
            if offset % 16:
                offset += 16 - (offset % 16)
            offset += 16 * count
            continue
        if offset % 16 + size > 16:
            offset += 16 - (offset % 16)
        offset += size
    if offset % 16:
        offset += 16 - (offset % 16)
    return offset


def cpp_members(source: str, macros: dict[str, int]) -> list[tuple[str, str, int]]:
    """(name, scala="float", float count) for AzyParams, ignoring padding members.

    Array dimensions are given as float counts so they can be compared with the
    shader's vector types directly: `float pass[4][4]` and `float4 g_pass[4]` are
    both 16 floats.
    """
    body = block(source, r"struct\s+AzyParams\s*\{")
    members: list[tuple[str, str, int]] = []
    for line in body.splitlines():
        line = line.split("//")[0].strip()
        match = re.match(r"^float\s+(\w+)\s*((?:\[[^\]]*\])*)\s*;$", line)
        if not match:
            continue
        name, dims = match.groups()
        if name in {"pad", "unused"} or name.endswith("_pad"):
            continue
        total = 1
        for part in re.findall(r"\[([^\]]*)\]", dims):
            part = part.strip()
            if part.isdigit():
                total *= int(part)
            else:
                total *= macros.get(part, -1000)
        members.append((name, "float", total))
    return members


def slot_constants(text: str, pattern: str) -> int | None:
    match = re.search(pattern, text)
    return int(match.group(1)) if match else None


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent
    shader_path = root / "resources" / "shaders" / "gloss.hlsl"
    header_path = root / "include" / "azy" / "win32" / "gloss" / "gloss_overlay.hpp"
    impl_path = root / "src" / "win32" / "gloss" / "gloss_overlay.cpp"

    shader = read(shader_path)
    header = read(header_path)
    impl = read(impl_path)
    if FAILURES:
        report()
        return 1

    # 1. Slot counts, both sides.
    passes = slot_constants(shader, r"#define\s+PASS_SLOTS\s+(\d+)")
    panels = slot_constants(shader, r"#define\s+PANEL_SLOTS\s+(\d+)")
    cpp_passes = slot_constants(header, r"kOverlayPassSlots\s*=\s*(\d+)")
    cpp_panels = slot_constants(header, r"kOverlayPanelSlots\s*=\s*(\d+)")
    if None in (passes, panels, cpp_passes, cpp_panels):
        fail("the region slot counts could not be read from both sides")
    else:
        if passes != cpp_passes:
            fail(f"PASS_SLOTS is {passes} in the shader but kOverlayPassSlots is {cpp_passes}")
        if panels != cpp_panels:
            fail(f"PANEL_SLOTS is {panels} in the shader but kOverlayPanelSlots is {cpp_panels}")

    # 2. Member lists line up, in order and in type.
    hlsl = hlsl_members(shader, {"PASS_SLOTS": passes or 0, "PANEL_SLOTS": panels or 0})
    macros = {"kOverlayPassSlots": cpp_passes or 0, "kOverlayPanelSlots": cpp_panels or 0}
    cpp = cpp_members(impl, macros)

    expected_hlsl_count = len(cpp) + 1  # + g_unused, the padding member the shader needs
    if len(hlsl) != expected_hlsl_count:
        fail(f"the cbuffer has {len(hlsl)} members but AzyParams has {len(cpp)} (+1 padding)")
    else:
        cpp_index = 0
        for index, (h_name, h_kind, h_count) in enumerate(hlsl):
            if h_name == "g_unused":
                continue  # mirrored by the C++ pad[], which is skipped on purpose
            c_name, _, c_floats = cpp[cpp_index]
            cpp_index += 1
            if h_name != "g_" + c_name:
                fail(f"member {index}: shader has '{h_name}', C++ has '{c_name}'")
                continue
            h_floats = HLSL_TYPES[h_kind][0] * h_count
            if h_floats != c_floats:
                fail(f"member {index} ({h_name}): the shader holds {h_floats} floats, the C++ holds {c_floats}")
        if cpp_index != len(cpp):
            fail("the C++ struct has members the shader's buffer does not")

    # 3. No float3 anywhere in the buffer: a float3 leaves padding the CPU side
    #    cannot see, which is how a constant buffer silently drifts.
    body = block(shader, r"cbuffer\s+AzyParams\s*:\s*register\(b0\)\s*\{")
    if re.search(r"\bfloat3\b", body):
        fail("the cbuffer contains a float3: use float4 plus an explicit scalar")

    # 4. The recursion guard: the capture must only ever be asked for a *window*,
    #    never for a monitor or the desktop. A desktop capture would include the
    #    duplicate window itself, which is how an infinite mirror starts.
    for path in sorted((root / "src").rglob("*.cpp")):
        text = read(path)
        if "CreateForMonitor" in text:
            fail(f"{path.relative_to(root)} calls CreateForMonitor: only window capture is allowed")
    guard = "GetCurrentProcessId"
    captures = read(root / "src" / "win32" / "capture" / "window_capture.cpp")
    if guard not in captures:
        fail("window_capture.cpp lost its own-process guard: Azy could be asked to mirror itself")

    # 5. The static_assert has to agree with the packing rules above.
    assert_match = re.search(r"static_assert\(sizeof\(AzyParams\)\s*==\s*(\d+)", impl)
    if not assert_match:
        fail("AzyParams has no static_assert on its size")
    else:
        declared = int(assert_match.group(1))
        computed = hlsl_layout(hlsl)
        if declared != computed:
            fail(f"the C++ static_assert says {declared} bytes but the HLSL layout computes to {computed}")

    report()
    return 1 if FAILURES else 0


def report() -> None:
    if FAILURES:
        print("overlay check: FAILED")
        for message in FAILURES:
            print(f"  - {message}")
    else:
        print("overlay check: shader, constant buffer and capture guard all agree")


if __name__ == "__main__":
    sys.exit(main())
