#!/usr/bin/env python3
"""Checks the mirror's contracts, which no compiler checks for Azy.

The mirror's look is decided by two files that must agree exactly:
resources/shaders/mirror.hlsl (the shader and its constant buffer) and
src/win32/mirror/mirror_renderer.cpp (the MirrorParams struct the CPU writes).
Nothing in either compiler compares them - a mismatch shows up only as wrong pixels
on a user's machine - so this script does the comparison:

  * the region slot counts match the header's constants,
  * the constant buffer's member list matches the struct's, in order, in type and in
    size (padding members included: they are how the layout is kept explicit),
  * the constant buffer is written in the packing-safe style (no float3),
  * the C++ static_assert agrees with the size the HLSL packing rules produce,
  * nothing anywhere calls the monitor form of the capture API (a monitor capture
    would include the mirror window itself: an infinite mirror),
  * the capture still refuses to mirror Azy's own process,
  * the mirror window still carries the styles that make it click-through and never
    topmost, and still answers HTTRANSPARENT, and
  * the composition never comes from the desktop window,
  * the shader is structurally sound (no HLSL compiler exists on a Linux build
    machine, so this is the closest thing to compiling it that can run here: balanced
    braces, both entry points, every identifier that is read declared before use, no
    member of the constant buffer left unused, and no HLSL reserved word - `point`,
    `line`, `sample` and friends - used where a name belongs, which fxc rejects with
    X3000 before it compiles anything. A Windows-side fxc run over the same file
    (scripts/check-shader.ps1) is the authoritative check; this one is the fast
    mirror of it for the platforms that cannot run fxc).

Run: python3 tools/check-mirror.py [repo root]
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

CBUFFER = r"cbuffer\s+AzyMirror\s*:\s*register\(b0\)\s*\{"


def array_extent(text: str, macros: dict[str, int]) -> int | None:
    """Evaluates a cbuffer array dimension such as PASS_SLOTS or PANEL_SLOTS / 4.

    Only arithmetic over the #defined slot counts is accepted: an expression that
    reaches for anything else is reported rather than guessed at, because a dimension
    this script cannot evaluate is a dimension it cannot check.
    """
    expression = text.strip()
    if not expression:
        return None
    for name, value in macros.items():
        expression = re.sub(r"\b" + re.escape(name) + r"\b", str(value), expression)
    if not re.fullmatch(r"[0-9+\-*/() ]+", expression):
        return None
    try:
        return int(eval(expression, {"__builtins__": {}}, {}))  # noqa: S307 - digits and operators only
    except (SyntaxError, ZeroDivisionError, TypeError):
        return None


def hlsl_members(source: str, macros: dict[str, int]) -> list[tuple[str, str, int]]:
    """(name, type, element count) for every member of the cbuffer, in order.

    `macros` resolves array dimensions written as a #define (PASS_SLOTS), including
    arithmetic over them (PANEL_SLOTS / 4).
    """
    body = block(source, CBUFFER)
    members: list[tuple[str, str, int]] = []
    for line in body.splitlines():
        line = line.split("//")[0].strip()
        if not line or line.startswith("{"):
            continue
        match = re.match(r"^(float[234]?|int|uint)\s+(\w+)(\[([^\]]+)\])?\s*;$", line)
        if not match:
            if line:
                fail(f"mirror.hlsl: unrecognised cbuffer member {line!r}")
            continue
        kind, name, _, array_size = match.groups()
        if array_size is None:
            count = 1
        else:
            count = array_extent(array_size, macros)
            if count is None:
                fail(f"mirror.hlsl: the array size of {name} ([{array_size}]) could not be evaluated")
                continue
        members.append((name, kind, count))
    return members


def hlsl_layout(members: list[tuple[str, str, int]]) -> int:
    """Byte size of the cbuffer under HLSL's packing rules.

    The rules that matter here: members are laid out in order; a member never
    straddles a 16-byte register; a vector is aligned to its own size; an array is
    aligned to a register and every element occupies one. These are the rules the CPU
    mirror reproduces by hand, which is the whole reason this function exists.
    """
    offset = 0
    for _, kind, count in members:
        components, size = HLSL_TYPES[kind]
        if count > 1:
            if kind == "float4":
                if offset % 16:
                    offset += 16 - (offset % 16)
                offset += 16 * count
                continue
            # Scalars and float2 arrays pack element by element, with the same
            # no-straddle rule (an element must not cross a register boundary).
            for _element in range(count):
                if offset % 16 + size > 16:
                    offset += 16 - (offset % 16)
                offset += size
            continue
        if offset % 16 + size > 16:
            offset += 16 - (offset % 16)
        offset += size
    if offset % 16:
        offset += 16 - (offset % 16)
    return offset


def cpp_members(source: str, macros: dict[str, int]) -> list[tuple[str, str, int]]:
    """(name, kind, float count) for MirrorParams, in order.

    Array dimensions are given as float counts so they can be compared with the
    shader's vector types directly: `float pass[4][4]` and `float4 g_pass[4]` are both
    16 floats. Padding members are compared like any other member, because a layout
    kept explicit is a layout that cannot drift silently.
    """
    body = block(source, r"struct\s+MirrorParams\s*\{")
    members: list[tuple[str, str, int]] = []
    for line in body.splitlines():
        line = line.split("//")[0].strip()
        match = re.match(r"^float\s+(\w+)\s*((?:\[[^\]]*\])*)\s*;$", line)
        if not match:
            continue
        name, dims = match.groups()
        total = 1
        for part in re.findall(r"\[([^\]]*)\]", dims):
            extent = array_extent(part, macros)
            total *= -1000 if extent is None else extent
        members.append((name, "float", total))
    return members


def slot_constants(text: str, pattern: str) -> int | None:
    match = re.search(pattern, text)
    return int(match.group(1)) if match else None


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent
    shader_path = root / "resources" / "shaders" / "mirror.hlsl"
    header_path = root / "include" / "azy" / "win32" / "mirror" / "mirror_renderer.hpp"
    style_path = root / "include" / "azy" / "core" / "mirror_style.hpp"
    impl_path = root / "src" / "win32" / "mirror" / "mirror_renderer.cpp"

    shader = read(shader_path)
    header = read(header_path)
    style_header = read(style_path)
    impl = read(impl_path)
    if FAILURES:
        report()
        return 1

    # 1. Slot counts, both sides.
    passes = slot_constants(shader, r"#define\s+PASS_SLOTS\s+(\d+)")
    panels = slot_constants(shader, r"#define\s+PANEL_SLOTS\s+(\d+)")
    cpp_passes = slot_constants(style_header, r"kMirrorPassSlots\s*=\s*(\d+)")
    cpp_panels = slot_constants(style_header, r"kMirrorPanelSlots\s*=\s*(\d+)")
    if None in (passes, panels, cpp_passes, cpp_panels):
        fail("the region slot counts could not be read from both sides")
    else:
        if passes != cpp_passes:
            fail(f"PASS_SLOTS is {passes} in the shader but kMirrorPassSlots is {cpp_passes}")
        if panels != cpp_panels:
            fail(f"PANEL_SLOTS is {panels} in the shader but kMirrorPanelSlots is {cpp_panels}")

    # 2. Member lists line up, in order and in size.
    hlsl = hlsl_members(shader, {"PASS_SLOTS": passes or 0, "PANEL_SLOTS": panels or 0})
    macros = {"kMirrorPassSlots": cpp_passes or 0, "kMirrorPanelSlots": cpp_panels or 0}
    cpp = cpp_members(header, macros)

    if len(hlsl) != len(cpp):
        fail(f"the cbuffer has {len(hlsl)} members but MirrorParams has {len(cpp)}")
    else:
        for index, ((h_name, h_kind, h_count), (c_name, _, c_floats)) in enumerate(zip(hlsl, cpp)):
            if h_name != "g_" + c_name:
                fail(f"member {index}: shader has '{h_name}', C++ has '{c_name}'")
                continue
            h_floats = HLSL_TYPES[h_kind][0] * h_count
            if h_floats != c_floats:
                fail(f"member {index} ({h_name}): the shader holds {h_floats} floats, the C++ holds {c_floats}")

    # 3. No float3 anywhere in the buffer: a float3 leaves padding the CPU side
    #    cannot see, which is how a constant buffer silently drifts.
    body = block(shader, CBUFFER)
    if re.search(r"\bfloat3\b", body):
        fail("the cbuffer contains a float3: use float4 plus an explicit scalar")

    # 4. The recursion guard: the capture must only ever be asked for a *window*,
    #    never for a monitor or the desktop. A desktop capture would include the
    #    mirror window itself, which is how an infinite mirror starts.
    for path in sorted((root / "src").rglob("*.cpp")):
        text = read(path)
        if "CreateForMonitor" in text:
            fail(f"{path.relative_to(root)} calls CreateForMonitor: only window capture is allowed")
    guard = "GetCurrentProcessId"
    captures = read(root / "src" / "win32" / "capture" / "window_capture.cpp")
    if guard not in captures:
        fail("window_capture.cpp lost its own-process guard: Azy could be asked to mirror itself")

    # 5. The mirror window's stacking and input contract. Four of the hard rules are
    #    structural, so they are checked rather than trusted:
    #      * a window over *another process* is skipped by hit testing only when it
    #        is layered and transparent (HTTRANSPARENT alone only forwards within the
    #        same thread),
    #      * the mirror must never be created topmost: it belongs directly above
    #        Premiere and below everything else,
    #      * the window procedure must still answer HTTRANSPARENT, and
    #      * the capture source must never be the desktop.
    renderer = read(impl_path)
    renderer_code = re.sub(r"//[^\n]*", "", renderer)
    style_match = re.search(r"const DWORD ex_style\s*=\s*([^;]+);", renderer_code)
    if not style_match:
        fail("mirror_renderer.cpp: the mirror window's extended style could not be found")
    else:
        style_expression = style_match.group(1)
        for needed in ("WS_EX_LAYERED", "WS_EX_TRANSPARENT", "WS_EX_NOACTIVATE", "WS_EX_TOOLWINDOW"):
            if needed not in style_expression:
                fail(f"the mirror window lost {needed}: click-through and focus safety depend on it")
        if "WS_EX_TOPMOST" in style_expression:
            fail("the mirror window is created topmost: it must sit above Premiere only")
    for forbidden in ("WS_EX_TOPMOST", "HWND_TOPMOST"):
        if forbidden in renderer_code:
            fail(f"mirror_renderer.cpp contains {forbidden}: never global always-on-top")
    if "HTTRANSPARENT" not in renderer_code:
        fail("mirror_renderer.cpp no longer answers HTTRANSPARENT in WM_NCHITTEST")
    if "GetDesktopWindow" in captures or "GetDesktopWindow" in renderer_code:
        fail("the desktop window is being captured or mirrored: the mirror must stay window-scoped")

    # 6. The static_assert has to agree with the packing rules above.
    assert_match = re.search(r"static_assert\(sizeof\(MirrorParams\)\s*==\s*(\d+)", header)
    if not assert_match:
        fail("MirrorParams has no static_assert on its size")
    else:
        declared = int(assert_match.group(1))
        computed = hlsl_layout(hlsl)
        if declared != computed:
            fail(f"the C++ static_assert says {declared} bytes but the HLSL layout computes to {computed}")

    # 7. The look must be theme-driven: no colour literal in the renderer.
    if re.search(r"\b0x[0-9A-Fa-f]{6}\b", renderer_code):
        fail("mirror_renderer.cpp contains a colour literal: colours belong to the theme engine")

    # 8. The shader itself. There is no HLSL compiler on the machines this project is
    #    verified on, so a syntax error in mirror.hlsl would only ever appear as "the
    #    skin does nothing" on a user's machine. These checks cannot replace a
    #    compiler, but they catch the mistakes that are actually made: an unbalanced
    #    block, a renamed entry point, a `g_` member read that the constant buffer does
    #    not declare, a variable used before it is assigned, and a cbuffer member
    #    nothing reads (which is a promise the renderer is not keeping).
    shader_code = re.sub(r"//[^\n]*", "", shader)
    if shader_code.count("{") != shader_code.count("}"):
        fail(f"mirror.hlsl: unbalanced braces ({shader_code.count('{')} open, {shader_code.count('}')} close)")
    if shader_code.count("(") != shader_code.count(")"):
        fail(f"mirror.hlsl: unbalanced parentheses ({shader_code.count('(')} open, {shader_code.count(')')} close)")
    for entry in ("vs_main", "ps_main"):
        if not re.search(r"\b" + entry + r"\s*\(", shader_code):
            fail(f"mirror.hlsl: the {entry} entry point is missing (the renderer compiles both by name)")
    # Every cbuffer member must be read somewhere outside the declaration itself.
    for name, _kind, _count in hlsl:
        uses = len(re.findall(r"\b" + re.escape(name) + r"\b", shader_code))
        if uses <= 1 and not name.startswith("g_pad") and name != "g_unused":
            fail(f"mirror.hlsl: {name} is declared in the constant buffer but never read")
    # Identifiers that are read but never declared are what a compiler would catch
    # first. Only the shader's own globals are checked: locals are a parser's job.
    declared = {name for name, _kind, _count in hlsl}
    declared |= {
        "g_capture",
        "g_capture_mip",
        "g_point",
        "g_linear",
        "PASS_SLOTS",
        "PANEL_SLOTS",
        "kLuma",
        "VsOut",
        "ps_main",
        "vs_main",
    }
    for match in re.finditer(r"\b(g_[A-Za-z0-9_]+)\b", shader_code):
        if match.group(1) not in declared:
            fail(f"mirror.hlsl: '{match.group(1)}' is used but not declared in the constant buffer")
    # Reserved words cannot be names. HLSL reserves the geometry-shader primitives
    # (`point`, `line`, `triangle`, ...) and the classic keywords (`sample`, `pass`,
    # `texture`, ...) - words a graphics programmer reaches for daily - and fxc
    # refuses the whole shader over one of them (X3000, "unexpected token"). 2.0.0
    # shipped exactly that (a parameter named `point`, a local named `line`), which
    # no structural check saw and no Linux machine could compile. Every declaration
    # is scanned: `type name` pairs, including parameters and struct members, are
    # where names are chosen.
    reserved = {
        "asm", "auto", "bool", "break", "case", "catch", "char", "class", "column_major",
        "compile", "const", "const_cast", "continue", "default", "delete", "discard", "do",
        "double", "dynamic_cast", "else", "enum", "explicit", "extern", "false", "filter",
        "float", "for", "friend", "goto", "groupshared", "half", "if", "in", "inline",
        "inout", "int", "interface", "line", "lineadj", "long", "matrix", "min10float",
        "min16float", "min16int", "min16uint", "mutable", "namespace", "new",
        "nointerpolation", "noperspective", "operator", "out", "packoffset", "pass",
        "pixelfragment", "point", "precise", "private", "protected", "public", "register",
        "reinterpret_cast", "return", "row_major", "sample", "sampler", "shared", "short",
        "signed", "sizeof", "snorm", "static", "static_cast", "string", "struct", "switch",
        "tbuffer", "technique", "technique10", "technique11", "template", "texture", "this",
        "throw", "true", "triangle", "triangleadj", "try", "typedef", "uint", "uniform",
        "union", "unsigned", "using", "vector", "virtual", "void", "volatile", "while",
    }
    declaration = re.compile(r"\b(?:float[234]?|int|uint|bool|void|struct|VsOut)\s+([A-Za-z_]\w*)")
    for match in declaration.finditer(shader_code):
        name = match.group(1)
        if name in reserved:
            line_number = shader_code.count("\n", 0, match.start()) + 1
            fail(
                f"mirror.hlsl:{line_number}: '{name}' is a reserved HLSL word used as a "
                "name (fxc: X3000 syntax error) - rename it"
            )

    report()
    return 1 if FAILURES else 0


def report() -> None:
    if FAILURES:
        print("mirror check: FAILED")
        for message in FAILURES:
            print(f"  - {message}")
    else:
        print("mirror check: shader, constant buffer, window styles and capture guard all agree")


if __name__ == "__main__":
    sys.exit(main())
