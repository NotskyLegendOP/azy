#!/usr/bin/env python3
"""Preview renderer for Azy Skin's ring.

Reproduces the maths in src/win32/skin/gdiplus_renderer.cpp and the palette in
src/core/theme.cpp (defaults) so the visual design can be reviewed without a
Windows machine. It composites the ring over a mock dark "Premiere-like" UI.

This is a design preview, not a screenshot: GDI+ anti-aliasing is approximated
with an analytic coverage function, which is what GDI+ does for these shapes.
"""

import struct
import zlib
from pathlib import Path

# ---------------------------------------------------------------- palette math


def clamp01(v):
    return 0.0 if v < 0 else (1.0 if v > 1 else v)


def to_byte(v):
    return int(clamp01(v / 255.0) * 255.0 + 0.5)


def mix(a, b, t):
    t = clamp01(t)
    return tuple(to_byte(a[i] * (1 - t) + b[i] * t) for i in range(len(a)))


CHARCOAL_LIFTED = (46, 47, 51)
CHARCOAL_DEEP = (11, 11, 13)
SURFACE_LIFTED = (54, 55, 60)
SURFACE_DEEP = (16, 16, 19)
WHITE = (255, 255, 255)
BLACK = (0, 0, 0)


def palette(theme="glass", glass=0.55, border=0.6, shadow=0.4, darkness=0.5, radius=8, perf=False):
    base = mix(CHARCOAL_LIFTED, CHARCOAL_DEEP, darkness)
    surface = mix(SURFACE_LIFTED, SURFACE_DEEP, darkness)
    glassy = theme == "glass"
    p = {}
    p["caption"] = mix(base, surface, 0.35)
    p["frame_border"] = (*mix(WHITE, base, 0.55), to_byte((0.06 + 0.10 * border) * 255))
    p["fill"] = (*surface, 255 if not glassy else to_byte((0.94 - 0.16 * glass) * 255))
    p["bezel"] = (*mix(WHITE, base, 0.70), to_byte((0.05 + 0.12 * border) * 255))
    p["border"] = (*mix(WHITE, surface, 0.35), to_byte((0.04 + 0.09 * border) * 255))
    p["highlight"] = (*WHITE, to_byte((0.02 + 0.05 * border) * 255))
    p["shadow"] = (*BLACK, to_byte((0.10 + 0.22 * shadow) * 255))
    p["radius"] = radius
    if perf:
        p["fill"] = (*surface, 0)
        p["shadow"] = (*BLACK, 0)
        p["radius"] = 0
    return p


# ---------------------------------------------------------------- raster canvas


class Canvas:
    def __init__(self, width, height, background=(32, 32, 34, 255)):
        self.w = width
        self.h = height
        self.px = [list(background) for _ in range(width * height)]

    def blend(self, x, y, color, coverage):
        if coverage <= 0 or x < 0 or y < 0 or x >= self.w or y >= self.h:
            return
        alpha = (color[3] / 255.0) * min(1.0, coverage)
        if alpha <= 0:
            return
        index = y * self.w + x
        dst = self.px[index]
        for i in range(3):
            dst[i] = int(dst[i] * (1 - alpha) + color[i] * alpha + 0.5)

    def fill_rect(self, x0, y0, x1, y1, color):
        for y in range(max(0, y0), min(self.h, y1)):
            for x in range(max(0, x0), min(self.w, x1)):
                self.blend(x, y, color, 1.0)


def rounded_rect_coverage(px, py, x0, y0, x1, y1, radius):
    """Analytic coverage of a rounded rectangle edge (GDI+ AA approximation)."""
    cx = min(max(px, x0 + radius), x1 - radius)
    cy = min(max(py, y0 + radius), y1 - radius)
    dx = px - cx
    dy = py - cy
    distance = (dx * dx + dy * dy) ** 0.5 - radius
    if x0 + radius <= px <= x1 - radius and y0 <= py <= y1:
        distance = min(distance, max(y0 - py, py - y1), 0.0)
    if y0 + radius <= py <= y1 - radius and x0 <= px <= x1:
        distance = min(distance, max(x0 - px, px - x1), 0.0)
    return clamp01(0.5 - distance)


def stroke_rounded(canvas, x0, y0, x1, y1, radius, color, thickness=1.0):
    """Coverage-limited stroke: only pixels within `thickness` of the boundary."""
    left = int(x0 - thickness - 2)
    top = int(y0 - thickness - 2)
    right = int(x1 + thickness + 2)
    bottom = int(y1 + thickness + 2)
    for y in range(max(0, top), min(canvas.h, bottom)):
        for x in range(max(0, left), min(canvas.w, right)):
            px, py = x + 0.5, y + 0.5
            outer = rounded_rect_coverage(px, py, x0, y0, x1, y1, radius)
            inner = rounded_rect_coverage(px, py, x0 + thickness, y0 + thickness, x1 - thickness,
                                          y1 - thickness, max(0.0, radius - thickness))
            coverage = max(0.0, outer - inner)
            canvas.blend(x, y, color, coverage)


# ---------------------------------------------------------------- the ring


def draw_ring(canvas, x0, y0, width, height, p, band=10, scale=1.0):
    """Mirrors GdiPlusRenderer::render: bezel row, then a quadratic shadow
    falloff starting one pixel in, with a subtle wash on top."""
    radius = float(p["radius"]) * scale
    draw_shadow = p["shadow"][3] > 0
    wash_depth = max(2, min(band, 6))
    fill = p["fill"]
    shadow = p["shadow"]
    bezel = p["bezel"]

    for depth in range(band + 1, 0, -1):
        if depth == 1:
            if bezel[3] > 0:
                stroke_rounded(canvas, x0 + 0, y0 + 0, x0 + width, y0 + height, radius, bezel)
            continue

        offset = (depth - 2) / band
        edge_strength = 1.0 - offset
        strength = edge_strength * edge_strength if draw_shadow else 0.0
        color = (*shadow[:3], to_byte(shadow[3] * strength)) if strength > 0 else (0, 0, 0, 0)

        wash = 1.0 - (depth - 1) / wash_depth if depth <= wash_depth else 0.0
        if wash > 0 and fill[3] > 0:
            wash_color = (*fill[:3], to_byte(fill[3] * wash * wash))
            total = color[3] + wash_color[3]
            if total > 0:
                m = wash_color[3] / total
                color = (
                    to_byte(color[0] * (1 - m) + wash_color[0] * m),
                    to_byte(color[1] * (1 - m) + wash_color[1] * m),
                    to_byte(color[2] * (1 - m) + wash_color[2] * m),
                    min(255, total),
                )
        if color[3] == 0:
            continue
        inset = float(depth - 2)
        stroke_rounded(canvas, x0 + inset, y0 + inset, x0 + width - inset, y0 + height - inset,
                       max(0.0, radius - inset), color)

    # hairline border on the frame edge, drawn last
    stroke_rounded(canvas, x0, y0, x0 + width, y0 + height, radius, p["border"])
    # barely-visible top inner highlight
    if p["highlight"][3] > 0:
        hy = int(y0 + band)
        for x in range(int(x0 + max(radius, 2) + 2), int(x0 + width - max(radius, 2) - 2)):
            canvas.blend(x, hy, p["highlight"], 1.0)


# ---------------------------------------------------------------- mock UI


def draw_mock_premiere(canvas, x0, y0, width, height, p, title="Azy Skin — design preview"):
    """A deliberately generic dark editing UI, to judge the ring in context."""
    panel = (42, 42, 45, 255)
    panel_dark = (36, 36, 39, 255)
    line = (58, 58, 62, 255)
    text = (150, 152, 160, 255)

    canvas.fill_rect(x0, y0, x0 + width, y0 + height, (30, 30, 32, 255))
    # caption / title bar
    canvas.fill_rect(x0, y0, x0 + width, y0 + 26, (*p["caption"], 255))
    canvas.fill_rect(x0 + 10, y0 + 11, x0 + 120, y0 + 15, text)

    body_y = y0 + 27
    canvas.fill_rect(x0, body_y, x0 + width, y0 + height, panel_dark)
    # left panel + right panel + bottom timeline, separated by hairlines
    canvas.fill_rect(x0 + 8, body_y + 8, x0 + 200, y0 + height - 90, panel)
    canvas.fill_rect(x0 + 208, body_y + 8, x0 + width - 8, body_y + 150, panel)
    canvas.fill_rect(x0 + 208, body_y + 158, x0 + width - 8, y0 + height - 90, panel)
    canvas.fill_rect(x0 + 8, y0 + height - 82, x0 + width - 8, y0 + height - 8, panel)
    for x in (x0 + 202, x0 + 204):
        canvas.fill_rect(x, body_y, x + 1, y0 + height, line)
    for y in (body_y + 152, y0 + height - 86):
        canvas.fill_rect(x0, y, x0 + width, y + 1, line)
    # some "clips"
    for i, colour in enumerate([(46, 58, 72, 255), (58, 46, 62, 255), (44, 60, 52, 255)]):
        canvas.fill_rect(x0 + 20 + i * 70, y0 + height - 60, x0 + 76 + i * 70, y0 + height - 26, colour)
    canvas.fill_rect(x0 + 24, body_y + 18, x0 + 120, body_y + 24, text)


def write_png(path, canvas):
    raw = bytearray()
    for y in range(canvas.h):
        raw.append(0)
        for x in range(canvas.w):
            r, g, b, _ = canvas.px[y * canvas.w + x]
            raw += bytes((r, g, b))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", canvas.w, canvas.h, 8, 2, 0, 0, 0)
    Path(path).write_bytes(
        b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) +
        chunk(b"IEND", b"")
    )


def window_panel(canvas, x0, y0, width, height, p, title, band=10):
    draw_mock_premiere(canvas, x0, y0, width, height, p, title)
    draw_ring(canvas, x0, y0, width, height, p, band=band)


def main():
    # Three previews side by side: default glass, Azy Dark (opaque), performance mode.
    gutter = 24
    win_w, win_h = 620, 420
    total_w = win_w * 3 + gutter * 4
    total_h = win_h + gutter * 2

    canvas = Canvas(total_w, total_h, (18, 18, 20, 255))
    glass = palette("glass")
    dark = palette("dark")
    perf = palette("perf", perf=True)

    window_panel(canvas, gutter, gutter, win_w, win_h, glass, "Azy Dark Glass (default)")
    window_panel(canvas, gutter * 2 + win_w, gutter, win_w, win_h, dark, "Azy Dark (opaque)")
    window_panel(canvas, gutter * 3 + win_w * 2, gutter, win_w, win_h, perf, "Performance mode", band=3)

    write_png("/tmp/azy_preview_themes.png", canvas)

    # Corner detail at 4x, so a 1px hairline and the falloff can be judged.
    zoom = 4
    detail = 60
    corners = Canvas(detail * zoom * 2 + 30, detail * zoom + 20, (18, 18, 20, 255))
    inner = Canvas(detail, detail, (30, 30, 32, 255))
    draw_mock_premiere(inner, 0, 0, detail, detail, glass, "")
    draw_ring(inner, 0, 0, detail, detail, glass)
    inner2 = Canvas(detail, detail, (30, 30, 32, 255))
    draw_mock_premiere(inner2, 0, 0, detail, detail, glass, "")   # without the ring
    for y in range(detail):
        for x in range(detail):
            for canvas_src, x_off in ((inner2, 10), (inner, detail * zoom + 20)):
                r, g, b, _ = canvas_src.px[y * detail + x]
                for dy in range(zoom):
                    for dx in range(zoom):
                        corners.px[(y * zoom + dy + 10) * corners.w + (x * zoom + dx + x_off)] = [r, g, b, 255]
    write_png("/tmp/azy_preview_corner_4x.png", corners)

    print("wrote /tmp/azy_preview_themes.png and /tmp/azy_preview_corner_4x.png")


if __name__ == "__main__":
    main()
