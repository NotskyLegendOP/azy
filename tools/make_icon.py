#!/usr/bin/env python3
"""Generates resources/azy_skin.ico - Azy Skin's application icon.

The icon is a dark rounded tile with a light "A", matching the icon the
application also draws at runtime (src/win32/ui/app_icon.cpp) so the tray, the
taskbar and Explorer all show the same mark.

Written as a standalone script with no third-party dependencies so the icon can
be regenerated at any time:

    python3 tools/make_icon.py

It emits a multi-resolution .ico (16/24/32/48/64/128/256) using BMP-encoded
frames, which every supported Windows version renders correctly.
"""

from __future__ import annotations

import struct
import zlib
from pathlib import Path

# Palette (matches the Azy Dark theme)
TILE = (28, 28, 32)
TILE_EDGE = (74, 76, 84)
GLYPH = (228, 229, 233)

SIZES = [16, 24, 32, 48, 64, 128, 256]


def rounded_tile(size: int) -> list[list[tuple[int, int, int, int]]]:
    """Premultiplied-independent RGBA tile with a 1px subtle edge."""
    inset = max(1, round(size * 0.06))
    radius = max(2, round(size * 0.22))
    pixels = [[(0, 0, 0, 0) for _ in range(size)] for _ in range(size)]

    for y in range(size):
        for x in range(size):
            # Signed distance to the rounded rectangle boundary.
            dx = max(inset - x, x - (size - 1 - inset), 0)
            dy = max(inset - y, y - (size - 1 - inset), 0)
            corner_x = max(inset + radius - x, x - (size - 1 - inset - radius), 0)
            corner_y = max(inset + radius - y, y - (size - 1 - inset - radius), 0)
            distance = ((corner_x**2 + corner_y**2) ** 0.5) - radius if (corner_x or corner_y) else -radius
            if dx or dy:
                distance = max(distance, (dx**2 + dy**2) ** 0.5)
            coverage = min(1.0, max(0.0, 0.5 - distance))
            if coverage <= 0.0:
                continue
            edge = distance > -1.2 and size >= 24
            base = TILE_EDGE if edge else TILE
            alpha = int(round(coverage * 255))
            pixels[y][x] = (base[0], base[1], base[2], alpha)
    return pixels


def draw_glyph(pixels: list[list[tuple[int, int, int, int]]]) -> None:
    """A geometric 'A': two strokes plus a crossbar. No font required."""
    size = len(pixels)
    height = size * 0.58
    width = size * 0.30
    thickness = max(1.0, size * 0.085)
    top_y = (size - height) / 2.0
    center_x = size / 2.0
    apex = (center_x, top_y)
    left_foot = (center_x - width / 2.0, top_y + height)
    right_foot = (center_x + width / 2.0, top_y + height)
    bar_y = top_y + height * 0.72

    def distance_to_segment(px: float, py: float, ax: float, ay: float, bx: float, by: float) -> float:
        vx, vy = bx - ax, by - ay
        wx, wy = px - ax, py - ay
        length_sq = vx * vx + vy * vy
        t = 0.0 if length_sq == 0 else max(0.0, min(1.0, (wx * vx + wy * vy) / length_sq))
        cx, cy = ax + t * vx, ay + t * vy
        return ((px - cx) ** 2 + (py - cy) ** 2) ** 0.5

    for y in range(size):
        for x in range(size):
            px, py = x + 0.5, y + 0.5
            d = min(
                distance_to_segment(px, py, *apex, *left_foot),
                distance_to_segment(px, py, *apex, *right_foot),
                distance_to_segment(px, py, center_x - width * 0.30, bar_y, center_x + width * 0.30, bar_y),
            )
            coverage = min(1.0, max(0.0, thickness / 2.0 - d + 0.5))
            if coverage <= 0.0:
                continue
            r, g, b, a = pixels[y][x]
            blend = coverage
            pixels[y][x] = (
                int(round(r * (1 - blend) + GLYPH[0] * blend)),
                int(round(g * (1 - blend) + GLYPH[1] * blend)),
                int(round(b * (1 - blend) + GLYPH[2] * blend)),
                a,
            )


def encode_png(pixels: list[list[tuple[int, int, int, int]]]) -> bytes:
    """PNG frame: supported inside .ico since Windows Vista, and ~25x smaller
    than an uncompressed 256x256 BMP frame."""
    size = len(pixels)
    raw = bytearray()
    for y in range(size):
        raw.append(0)  # filter: none
        for x in range(size):
            r, g, b, a = pixels[y][x]
            raw += bytes((r, g, b, a))

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (
            struct.pack(">I", len(data))
            + tag
            + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        )

    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)  # 8-bit RGBA
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b"")
    )


def encode_frame(pixels: list[list[tuple[int, int, int, int]]]) -> bytes:
    """BITMAPINFOHEADER + BGRA bottom-up pixels + 1bpp AND mask (32bpp icon)."""
    size = len(pixels)
    header = struct.pack(
        "<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, size * size * 4, 0, 0, 0, 0
    )
    body = bytearray()
    for y in range(size - 1, -1, -1):  # bottom-up
        for x in range(size):
            r, g, b, a = pixels[y][x]
            body += bytes((b, g, r, a))
    # AND mask: all zero (the alpha channel does the masking), padded to 4 bytes.
    stride = ((size + 31) // 32) * 4
    mask = bytes(stride * size)
    return header + bytes(body) + mask


def main() -> None:
    frames = []
    for size in SIZES:
        pixels = rounded_tile(size)
        draw_glyph(pixels)
        # PNG frames for the large sizes, BMP frames for the small ones (some
        # older shell paths still expect BMP at 16-48px).
        frames.append((size, encode_png(pixels) if size >= 128 else encode_frame(pixels)))

    directory = struct.pack("<HHH", 0, 1, len(frames))
    offset = 6 + 16 * len(frames)
    entries = bytearray()
    payload = bytearray()
    for size, data in frames:
        dimension = 0 if size >= 256 else size
        entries += struct.pack("<BBBBHHII", dimension, dimension, 0, 0, 1, 32, len(data), offset)
        payload += data
        offset += len(data)

    target = Path(__file__).resolve().parent.parent / "resources" / "azy_skin.ico"
    target.write_bytes(directory + bytes(entries) + bytes(payload))
    print(f"wrote {target} ({target.stat().st_size} bytes, {len(frames)} frames)")


if __name__ == "__main__":
    main()
