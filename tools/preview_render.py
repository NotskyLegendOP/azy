#!/usr/bin/env python3
"""Design preview of the mirror composition — a picture of the *maths*, not of Azy.

What this is:

  The skin is one GPU pass (resources/shaders/mirror.hlsl) over a live capture of the
  real Premiere Pro window. Nothing here can run that: there is no GPU, no Premiere
  and no Windows on the machine this repository is verified on. So this script does
  the next best thing - it reproduces the shader's arithmetic in numpy, applies it to
  a synthetic "before" picture of Premiere's workspace, and writes the result as a PNG.

  Two things make it worth trusting as far as it goes:

    * the numbers it uses (theme tokens, the mirror style, the panel model and the
      media pass-through rectangles) are *not* transcribed. It runs the real C++
      (tools/dump_style.cpp) and reads what the application would use, so a preview
      can never disagree with Azy about what a theme or a layout is;
    * the composition steps are in the same order, with the same classification and the
      same weights, as mirror.hlsl. Where the shader samples a mip level, this blurs by
      a box filter of the equivalent radius.

  What it is NOT:

    * not a screenshot, and not evidence about Premiere. The "before" picture is
      invented; the real window may differ in any way, and nothing about the capture,
      the window, the composition or the GPU is exercised here;
    * not a test of the shader. It cannot catch a syntax error in HLSL (tools/
      check-mirror.py does the structural checks it can) - only a compiler can;
    * not a guarantee about the GPU path's timing or cost.

Usage:

    pip install --target /tmp/pylibs numpy pillow       # optional, for this tool only
    cmake --build build-tests --target azy_dump_style
    PYTHONPATH=/tmp/pylibs python3 tools/preview_render.py docs/images

Nothing in the shipped application reads the images this writes.
"""

from __future__ import annotations

import argparse
import math
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

try:
    import numpy as np
    from PIL import Image, ImageDraw, ImageFont
except ImportError:  # pragma: no cover - the message is the point
    sys.exit(
        "preview_render.py needs numpy and pillow:\n"
        "  pip install --target /tmp/pylibs numpy pillow\n"
        "  PYTHONPATH=/tmp/pylibs python3 tools/preview_render.py docs/images"
    )

ROOT = Path(__file__).resolve().parent.parent


# --------------------------------------------------------------------------- dump


@dataclass
class Style:
    values: dict[str, float] = field(default_factory=dict)
    background: tuple[float, float, float] = (0.0, 0.0, 0.0)
    surface: tuple[float, float, float] = (0.0, 0.0, 0.0)
    border: tuple[float, float, float] = (0.0, 0.0, 0.0)
    accent: tuple[float, float, float] = (0.0, 0.0, 0.0)
    glow: tuple[float, float, float] = (0.0, 0.0, 0.0)
    name: str = ""
    theme_id: str = ""


def run_dump(tool: Path, client: tuple[int, int], theme: str) -> dict:
    result = subprocess.run(
        [str(tool), "--client-w", str(client[0]), "--client-h", str(client[1]), "--overlay-margin", "0"],
        capture_output=True,
        text=True,
        check=True,
    )
    sections: list[dict] = []

    def blank() -> dict:
        return {"style": {}, "token": {}, "panels": [], "media": [], "frames": [], "geometry": {}, "meta": {}}

    current = blank()
    for line in result.stdout.splitlines():
        if line == "--":
            sections.append(current)
            current = blank()
            continue
        parts = line.split()
        if len(parts) < 2:
            continue
        key, value = parts[0], " ".join(parts[1:])
        if key.startswith("style."):
            try:
                current["style"][key[len("style.") :]] = float(value)
            except ValueError:
                pass
        elif key.startswith("token."):
            try:
                current["token"][key[len("token.") :]] = float(value)
            except ValueError:
                pass
        elif key.startswith("theme."):
            current["meta"][key[len("theme.") :]] = value
        elif key == "geometry":
            current["geometry"][parts[1]] = [float(p) for p in parts[2:]]
        elif key == "panel":
            current["panels"].append([float(p) for p in parts[1:]])
        elif key == "region.media":
            current["media"].append([float(p) for p in parts[2:]])
        elif key == "region.panel":
            current["frames"].append([float(p) for p in parts[2:]])

    # The dump prints the geometry first and one section per theme after it, so the
    # sections with a style are the ones that matter (and the requested theme wins).
    styles = [section for section in sections if "base_dark" in section["style"]]
    if not styles:
        raise SystemExit("azy_dump_style produced no style section: build it first")
    chosen = styles[0]
    for section in styles:
        if section["meta"].get("id") == theme:
            chosen = section
    geometry = next((section for section in sections if section["panels"] or section["media"]), None)
    if geometry is not None:
        chosen["geometry"] = geometry["geometry"]
        chosen["panels"] = geometry["panels"]
        chosen["media"] = geometry["media"]
        chosen["frames"] = geometry["frames"]
    sections = styles

    style = Style()
    style.values = chosen["style"]
    style.name = chosen["meta"].get("name", "")
    style.theme_id = chosen["meta"].get("id", "")
    style.background = (style.values["background.r"], style.values["background.g"], style.values["background.b"])
    style.surface = (style.values["surface_colour.r"], style.values["surface_colour.g"], style.values["surface_colour.b"])
    style.border = (style.values["border_colour.r"], style.values["border_colour.g"], style.values["border_colour.b"])
    style.accent = (style.values["accent.r"], style.values["accent.g"], style.values["accent.b"])
    style.glow = (style.values["glow_colour.r"], style.values["glow_colour.g"], style.values["glow_colour.b"])
    return {"style": style, "panels": chosen["panels"], "media": chosen["media"], "frames": chosen["frames"],
            "geometry": chosen["geometry"], "token": chosen["token"], "all": sections}


# ------------------------------------------------------------- the "before" UI


def mock_premiere(width: int, height: int, panels: list[list[float]]) -> np.ndarray:
    """A synthetic 'before' picture: what a raw Premiere Pro workspace looks like.

    Deliberately Premiere-like rather than pretty: mid-dark grey panels, small light
    labels, a bright timeline with coloured clips, a green audio meter, and *media*
    inside the two monitors (a colourful picture, which is the part the skin must not
    touch).
    """
    image = Image.new("RGB", (width, height), (43, 43, 43))
    draw = ImageDraw.Draw(image)

    try:
        font = ImageFont.load_default(size=11)
        small = ImageFont.load_default(size=9)
    except TypeError:  # older pillow: no size argument
        font = ImageFont.load_default()
        small = font

    def label(x: int, y: int, text: str, colour=(168, 168, 168)) -> None:
        draw.text((x + 4, y + 4), text, fill=colour, font=small)

    def dashes(x0: int, y0: int, x1: int, y1: int, rows: int, colour=(150, 150, 150)) -> None:
        span = max(1, x1 - x0 - 12)
        step = max(6, span // 7)
        for row in range(rows):
            y = y0 + 8 + row * 13
            if y > y1 - 6:
                break
            x = x0 + 6
            while x < x1 - 6:
                length = 14 + ((x * 7 + row * 13) % 26)
                draw.rectangle([x, y, min(x + length, x1 - 6), y + 4], fill=colour)
                x += length + step

    # Premiere's own panel backgrounds, a step apart from each other.
    for index, panel in enumerate(panels):
        _panel_id, x, y, w, h, usable = [int(v) for v in panel]
        if not usable:
            continue
        if w <= 0 or h <= 0:
            continue
        shade = 56 if index % 2 == 0 else 50
        draw.rectangle([x, y, x + w - 1, y + h - 1], fill=(shade, shade, shade + 2))
        draw.rectangle([x, y, x + w - 1, y + 20], fill=(38, 38, 40))
        draw.rectangle([x, y + 20, x + w - 1, y + 20], fill=(24, 24, 26))
        label(x, y, f"panel {index}")

    # Header/menu bands: Premiere's own text on a slightly lighter strip.
    for panel in panels:
        _panel_id, x, y, w, h, usable = [int(v) for v in panel]
        if usable and y < 60:
            dashes(x, y, x + w, y + h, 1, (196, 196, 196))

    return image, draw, font, small


def paint_media(draw: ImageDraw.ImageDraw, rect: list[float]) -> np.ndarray:
    """A picture inside a monitor: this is what must come back untouched."""
    x0, y0, x1, y1 = [int(v) for v in rect]
    x1 = max(x0 + 1, x1)
    y1 = max(y0 + 1, y1)
    width, height = x1 - x0, y1 - y0
    image = Image.new("RGB", (width, height))
    pixels = image.load()
    for y in range(height):
        t = y / max(1, height - 1)
        sky = (int(38 + 120 * (1.0 - t)), int(58 + 110 * (1.0 - t)), int(92 + 120 * (1.0 - t)))
        for x in range(width):
            u = x / max(1, width - 1)
            # A warm sun, a cool horizon and a dark foreground: bright and dark areas
            # in the same frame, so the classification has something to get wrong.
            sun = max(0.0, 1.0 - math.hypot(u - 0.72, t - 0.30) * 6.0)
            horizon = max(0.0, 1.0 - abs(t - 0.62) * 18.0)
            r = int(min(255, sky[0] + sun * 210 + horizon * 60))
            g = int(min(255, sky[1] + sun * 170 + horizon * 40))
            b = int(min(255, sky[2] + sun * 90 + horizon * 20))
            if t > 0.66:
                shade = 0.35 - (t - 0.66)
                r, g, b = int(r * shade), int(g * shade), int(b * shade + 20)
            pixels[x, y] = (max(0, r), max(0, g), max(0, b))
    draw._image.paste(image, (x0, y0))
    return np.asarray(image, dtype=np.float32) / 255.0


def paint_timeline(draw: ImageDraw.ImageDraw, panels: list[list[float]]) -> None:
    """Clips, a playhead and audio meters: the area the brief calls key."""
    band = None
    for panel in panels:
        _id, x, y, w, h, usable = [int(v) for v in panel]
        if usable and h > 150 and y > 400:
            band = (x, y, w, h)
    if band is None:
        return
    x, y, w, h = band
    track_y = y + 30
    colours = [(72, 96, 140), (96, 72, 120), (60, 104, 92), (120, 96, 64)]
    clip_index = 0
    while track_y + 60 < y + h:
        draw.rectangle([x, track_y, x + w - 1, track_y + 54], fill=(34, 34, 36))
        cx = x + 6
        while cx < x + w - 30:
            cw = 60 + ((cx * 31) % 140)
            colour = colours[clip_index % len(colours)]
            draw.rectangle([cx, track_y + 3, min(cx + cw, x + w - 6), track_y + 51], fill=colour)
            draw.rectangle([cx, track_y + 3, min(cx + cw, x + w - 6), track_y + 8], fill=tuple(min(255, c + 40) for c in colour))
            cx += cw + 6
            clip_index += 1
        track_y += 60
    # Playhead: the brightest vertical line in the timeline.
    playhead = x + int(w * 0.62)
    draw.rectangle([playhead, y + 22, playhead + 1, y + h - 4], fill=(232, 232, 240))
    # Audio meters: a green/amber ladder on the right of the band.
    meter_x = x + w - 34
    for i in range(18):
        level = 1.0 - i / 18.0
        colour = (70, 190, 90) if level < 0.55 else ((210, 190, 70) if level < 0.8 else (200, 70, 60))
        if i < 12:
            draw.rectangle([meter_x, y + 30 + i * 12, meter_x + 10, y + 30 + i * 12 + 8], fill=colour)
        if i < 8:
            draw.rectangle([meter_x + 14, y + 30 + i * 12, meter_x + 24, y + 30 + i * 12 + 8], fill=colour)


def paint_project_panel(draw: ImageDraw.ImageDraw, panels: list[list[float]]) -> None:
    """Thumbnails in a bin: small pictures the skin must also leave alone."""
    for panel in panels:
        _id, x, y, w, h, usable = [int(v) for v in panel]
        if not usable or w > 500 or h < 150:
            continue
        grid_y = y + 28
        while grid_y + 60 < y + h:
            grid_x = x + 8
            while grid_x + 90 < x + w - 8:
                tint = ((grid_x * 13) % 120, (grid_y * 7) % 120, (grid_x + grid_y) % 140)
                draw.rectangle([grid_x, grid_y, grid_x + 84, grid_y + 48],
                               fill=(60 + tint[0] // 3, 60 + tint[1] // 3, 70 + tint[2] // 3))
                draw.rectangle([grid_x, grid_y + 50, grid_x + 60, grid_y + 55], fill=(150, 150, 150))
                grid_x += 96
            grid_y += 72


# ----------------------------------------------------------------- the shader


def blur(image: np.ndarray, radius: int) -> np.ndarray:
    """Box blur, the mip level's stand-in: same idea, cheap and honest about it."""
    if radius < 1:
        return image
    kernel = radius * 2 + 1
    padded = np.pad(image, ((radius, radius), (radius, radius), (0, 0)), mode="edge")
    cumulative = padded.cumsum(axis=0).cumsum(axis=1)
    cumulative = np.pad(cumulative, ((1, 0), (1, 0), (0, 0)), mode="constant")
    height, width = image.shape[0], image.shape[1]
    total = (
        cumulative[kernel : kernel + height, kernel : kernel + width]
        - cumulative[0:height, kernel : kernel + width]
        - cumulative[kernel : kernel + height, 0:width]
        + cumulative[0:height, 0:width]
    )
    return total / float(kernel * kernel)


def sd_rounded_box(px: np.ndarray, py: np.ndarray, half_x: float, half_y: float, radius: float) -> np.ndarray:
    qx = np.abs(px) - half_x + radius
    qy = np.abs(py) - half_y + radius
    return np.minimum(np.maximum(qx, qy), 0.0) + np.hypot(np.maximum(qx, 0.0), np.maximum(qy, 0.0)) - radius


def compose(raw: np.ndarray, style: Style, media: list[list[float]], frames: list[list[float]],
            performance_mode: bool = False, fade: float = 1.0) -> np.ndarray:
    """The steps of mirror.hlsl, in order, on a CPU."""
    values = style.values
    height, width = raw.shape[0], raw.shape[1]
    xs = np.arange(width, dtype=np.float32)[None, :]
    ys = np.arange(height, dtype=np.float32)[:, None]
    pixel_x = np.broadcast_to(xs, (height, width))
    pixel_y = np.broadcast_to(ys, (height, width))
    size_x, size_y = float(width), float(height)

    def scaled(name: str) -> float:
        return values[name] * fade

    captured = raw
    k_luma = np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
    captured_luma = captured @ k_luma

    # media pass-through: a 1.5px ramp *inside* the rectangle
    passthrough = np.zeros((height, width), dtype=np.float32)
    for rect in media:
        x0, y0, x1, y1 = rect
        to_edge_x = np.minimum(pixel_x - x0, x1 - pixel_x)
        to_edge_y = np.minimum(pixel_y - y0, y1 - pixel_y)
        passthrough = np.maximum(passthrough, np.clip(np.minimum(to_edge_x, to_edge_y) / 1.5, 0.0, 1.0))
    # Content protection: bright *and* vividly coloured pixels inside a panel are far
    # more likely to be a thumbnail, a preview image or a waveform overlay than a
    # control, so they escape most of the treatment. Same thresholds as the shader.
    channel_max = captured.max(axis=2)
    channel_min = captured.min(axis=2)
    saturation = channel_max - channel_min

    def smoothstep(edge0: float, edge1: float, value: np.ndarray) -> np.ndarray:
        t = np.clip((value - edge0) / max(1e-5, edge1 - edge0), 0.0, 1.0)
        return t * t * (3.0 - 2.0 * t)

    content = smoothstep(0.30, 0.55, saturation) * smoothstep(0.45, 0.75, captured_luma)
    keep = np.maximum(passthrough, content * values["content_keep"])
    ui = 1.0 - keep

    # panel membership + edge ring
    in_panel = np.zeros((height, width), dtype=np.float32)
    panel_edge = np.zeros((height, width), dtype=np.float32)
    panel_origin_x = np.zeros((height, width), dtype=np.float32)
    panel_origin_y = np.zeros((height, width), dtype=np.float32)
    panel_extent_x = np.full((height, width), size_x, dtype=np.float32)
    panel_extent_y = np.full((height, width), size_y, dtype=np.float32)
    radius = values["radius"]
    for rect in frames:
        x0, y0, x1, y1 = rect
        half_x, half_y = (x1 - x0) * 0.5, (y1 - y0) * 0.5
        r = min(radius, min(half_x, half_y) - 1.0)
        distance = sd_rounded_box(pixel_x - (x0 + x1) * 0.5, pixel_y - (y0 + y1) * 0.5, half_x, half_y, r)
        inside = np.clip(0.5 - distance, 0.0, 1.0)
        take = inside > in_panel
        in_panel = np.where(take, inside, in_panel)
        panel_origin_x = np.where(take, x0, panel_origin_x)
        panel_origin_y = np.where(take, y0, panel_origin_y)
        panel_extent_x = np.where(take, x1 - x0, panel_extent_x)
        panel_extent_y = np.where(take, y1 - y0, panel_extent_y)
        panel_edge = np.maximum(panel_edge, np.clip(1.0 - np.abs(distance) / 1.0, 0.0, 1.0) * (distance >= -1.0))

    surface_membership = np.maximum(in_panel, np.clip(1.0 - passthrough, 0.0, 1.0))
    is_surface = 1.0 - np.clip(
        (captured_luma - (values["transition"] - values["boundary_soft"]))
        / max(1e-5, 2.0 * values["boundary_soft"]),
        0.0,
        1.0,
    )
    is_surface = is_surface * is_surface * (3.0 - 2.0 * is_surface)  # smoothstep, matching the shader

    colour = captured.copy()
    diffused = blur(captured, 16)
    blend = np.clip(scaled("glass") * is_surface * surface_membership * ui, 0.0, 1.0)[..., None]
    colour = colour + (diffused - colour) * blend
    blend = np.clip(scaled("base_dark") * ui, 0.0, 1.0)[..., None]
    colour = colour + (np.array(style.background, dtype=np.float32)[None, None, :] - colour) * blend
    blend = np.clip(scaled("surface") * is_surface * surface_membership * ui, 0.0, 1.0)[..., None]
    colour = colour + (np.array(style.surface, dtype=np.float32)[None, None, :] - colour) * blend

    # clarity, mid-tone recovery and the border detector (four neighbour taps)
    offset = values["clarity_offset"] * max(values["dpi"], 1.0)
    step = int(round(offset))
    north = np.roll(captured, -step, axis=0)
    south = np.roll(captured, step, axis=0)
    west = np.roll(captured, -step, axis=1)
    east = np.roll(captured, step, axis=1)
    local_mean = (north @ k_luma + south @ k_luma + west @ k_luma + east @ k_luma) * 0.25
    gradient = np.maximum(
        np.abs(captured_luma - local_mean) * 2.0,
        np.maximum(np.abs(north @ k_luma - captured_luma), np.abs(east @ k_luma - captured_luma)),
    )
    active = ui > 0.001
    colour = colour + (np.stack([captured_luma - local_mean] * 3, axis=-1)) * (
        values["clarity"] * (0.5 + 0.5 * is_surface) * ui
    )[..., None] * active[..., None]

    band = np.clip((local_mean - 0.06) / max(1e-5, values["mid_tone"] - 0.06), 0.0, 1.0)
    band = band * band * (3.0 - 2.0 * band)
    upper = np.clip((local_mean - values["mid_tone"]) / max(1e-5, 0.95 - values["mid_tone"]), 0.0, 1.0)
    band = band * (1.0 - upper * upper * (3.0 - 2.0 * upper))
    colour = colour + colour * (scaled("mid_boost") * band * ui)[..., None] * active[..., None]

    line = np.clip(gradient / max(values["transition"], 0.05), 0.0, 1.0) * scaled("highlight")
    border_ink = np.array(
        [style.border[i] + (style.accent[i] - style.border[i]) * min(1.0, values["accent_mix"]) for i in range(3)],
        dtype=np.float32,
    )
    colour = colour + border_ink[None, None, :] * (line * ui)[..., None] * active[..., None]

    # the panel frame: ring, header band, corner glow
    ring = np.clip(panel_edge * scaled("highlight"), 0.0, 1.0)
    frame_ink = np.array(
        [style.border[i] + (style.accent[i] - style.border[i]) * min(1.0, values["accent_mix"] * 0.8) for i in range(3)],
        dtype=np.float32,
    )
    framed = (panel_edge > 0.0) & active
    colour = colour + frame_ink[None, None, :] * (ring * framed)[..., None]
    header = np.clip(1.0 - (pixel_y - panel_origin_y) / max(values["highlight_band"], 1.0), 0.0, 1.0)
    colour = colour + np.array(style.accent, dtype=np.float32)[None, None, :] * (
        header * 0.05 * min(1.0, scaled("glow") * 2.0) * in_panel * framed
    )[..., None]
    corner_x = (pixel_x - panel_origin_x) / np.maximum(panel_extent_x, 1.0)
    corner_y = (pixel_y - panel_origin_y) / np.maximum(panel_extent_y, 1.0)
    corner = np.clip(1.0 - np.hypot(corner_x * 2.2, corner_y * 3.4) * 2.0, 0.0, 1.0)
    colour = colour + np.array(style.glow, dtype=np.float32)[None, None, :] * (
        corner * corner * scaled("glow") * in_panel * framed
    )[..., None]

    # interior shadow + density
    edge_distance = np.minimum(np.minimum(pixel_x, size_x - pixel_x), np.minimum(pixel_y, size_y - pixel_y)) / max(
        values["shadow_soft"], 1.0
    )
    inset = np.clip(edge_distance, 0.0, 1.0)
    depth = np.clip((values["shadow"] + values["density"]) * (1.0 - inset) * values["surface"] * 2.0, 0.0, 1.0)
    colour = colour * (1.0 - depth * ui)[..., None] * active[..., None] + colour * (~active)[..., None]

    # global lighting: key light, gloss sweep, polish, vignette, grain
    normalized_x = pixel_x / size_x
    normalized_y = pixel_y / size_y
    key = np.clip(1.0 - normalized_y * 1.6, 0.0, 1.0)
    colour = colour + np.array(style.glow, dtype=np.float32)[None, None, :] * (
        key * key * scaled("key_light") * ui
    )[..., None]
    sweep = np.clip(1.0 - np.abs(normalized_x * 0.55 + normalized_y * 0.45 - 0.34) * 2.6, 0.0, 1.0)
    colour = colour + (scaled("gloss") * sweep * sweep * ui)[..., None]
    polish = np.clip(1.0 - np.abs(normalized_x - 0.18) * 3.4, 0.0, 1.0) * np.clip(
        1.0 - normalized_y * 2.2, 0.0, 1.0
    )
    colour = colour + (scaled("gloss") * polish * polish * 0.35 * ui)[..., None]
    radius_norm = np.hypot(pixel_x - size_x * 0.5, pixel_y - size_y * 0.5) / max(
        math.hypot(size_x * 0.5, size_y * 0.5), 1.0
    )
    colour = colour * (1.0 - scaled("vig") * np.clip(radius_norm * radius_norm, 0.0, 1.0) * ui)[..., None]

    if scaled("grain") > 0.0:
        grain = np.mod(np.sin(np.floor(pixel_x) * 12.9898 + np.floor(pixel_y) * 78.233) * 43758.5453, 1.0)
        colour = colour + ((grain - 0.5) * scaled("grain") * ui)[..., None]

    # the corner rounding of the window itself, then the media pass-through
    alpha = np.ones((height, width), dtype=np.float32)
    if radius > 0.5:
        distance = sd_rounded_box(pixel_x - size_x * 0.5, pixel_y - size_y * 0.5, size_x * 0.5 - 0.5,
                                  size_y * 0.5 - 0.5, min(radius, min(size_x, size_y) * 0.5 - 1.0))
        alpha = np.clip(0.5 - distance, 0.0, 1.0)
    colour = captured + (np.clip(colour, 0.0, 1.0) - captured) * ui[..., None]
    return np.clip(colour * alpha[..., None], 0.0, 1.0)


# ------------------------------------------------------------------------- main


def to_image(array: np.ndarray) -> Image.Image:
    return Image.fromarray((np.clip(array, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8), "RGB")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", nargs="?", default="docs/images")
    parser.add_argument("--dump-tool", default=str(ROOT / "build-tests" / "azy_dump_style"))
    parser.add_argument("--size", default="1600x900")
    parser.add_argument("--theme", default="blue_purple")
    args = parser.parse_args()

    tool = Path(args.dump_tool)
    if not tool.exists():
        raise SystemExit(f"{tool} not found - build it: cmake --build build-tests --target azy_dump_style")

    width, height = (int(v) for v in args.size.lower().split("x"))
    dump = run_dump(tool, (width, height), args.theme)
    style: Style = dump["style"]
    media = dump["media"]
    frames = dump["frames"]

    image, draw, font, small = mock_premiere(width, height, dump["panels"])
    paint_project_panel(draw, dump["panels"])
    paint_timeline(draw, dump["panels"])
    for rect in media:
        paint_media(draw, rect)
    raw = np.asarray(image, dtype=np.float32) / 255.0

    skinned = compose(raw, style, media, frames)

    # --- the numbers this preview can actually measure -----------------------
    media_delta = 0.0
    for rect in media:
        # Stay strictly inside the region: the shader ramps over the last 1.5 px of
        # the rectangle, so a comparison that touched the edge would be measuring the
        # ramp rather than the pass-through.
        x0, y0, x1, y1 = int(rect[0]) + 3, int(rect[1]) + 3, int(rect[2]) - 3, int(rect[3]) - 3
        region_raw = raw[max(0, y0) : y1, max(0, x0) : x1]
        region_out = skinned[max(0, y0) : y1, max(0, x0) : x1]
        if region_raw.size:
            media_delta = max(media_delta, float(np.abs(region_raw - region_out).max()))
    raw_luma = float((raw @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)).mean())
    out_luma = float((skinned @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)).mean())

    def text_contrast(picture: np.ndarray) -> float:
        # Local standard deviation over the left column, where the labels are: a
        # proxy for "is the text still readable", not a proof.
        column = picture[:, 0 : int(width * 0.2)]
        luma = column @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
        high = luma[:, 1:] - luma[:, :-1]
        return float(high.std())

    print(f"preview: {width}x{height}, theme '{style.name}' ({style.theme_id})")
    print(f"  media regions: {len(media)} | panel frames: {len(frames)}")
    print(f"  media pass-through: largest per-channel change inside a monitor = {media_delta:.4f} (0 = untouched)")
    print(f"  mean luminance: raw {raw_luma:.3f} -> skinned {out_luma:.3f} ({out_luma / max(raw_luma, 1e-6):.2f}x)")
    print(f"  text-region edge contrast: raw {text_contrast(raw):.4f} -> skinned {text_contrast(skinned):.4f}")

    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)

    # Panel 1: before and after, at full size.
    before_after = Image.new("RGB", (width, height * 2 + 12), (16, 16, 18))
    before_after.paste(to_image(raw), (0, 0))
    before_after.paste(to_image(skinned), (0, height + 12))
    draw_ba = ImageDraw.Draw(before_after)
    draw_ba.text((8, 4), "NO SKIN (the real window, untouched)", fill=(230, 230, 235), font=font)
    draw_ba.text((8, height + 16), f"AZY SKIN - live mirror through mirror.hlsl (theme: {style.name})",
                 fill=(230, 230, 235), font=font)
    before_after.save(output / "azy_mirror_before_after.png")

    # Panel 2: every theme, from the same C++ values.
    names = [s["meta"].get("name", "") for s in dump["all"]]
    ids = [s["meta"].get("id", "") for s in dump["all"]]
    tile = 2
    tile_w, tile_h = width // tile, height // tile
    columns, rows = 3, 3
    sheet = Image.new("RGB", (tile_w * columns + 8 * (columns + 1), (tile_h + 22) * rows + 8 * (rows + 1)), (14, 14, 16))
    draw_sheet = ImageDraw.Draw(sheet)
    index = 0
    for section in dump["all"]:
        if section["meta"].get("id") == "original":
            continue
        style_variant = Style()
        style_variant.values = section["style"]
        style_variant.background = (
            style_variant.values["background.r"], style_variant.values["background.g"], style_variant.values["background.b"])
        style_variant.surface = (
            style_variant.values["surface_colour.r"], style_variant.values["surface_colour.g"],
            style_variant.values["surface_colour.b"])
        style_variant.border = (
            style_variant.values["border_colour.r"], style_variant.values["border_colour.g"],
            style_variant.values["border_colour.b"])
        style_variant.accent = (style_variant.values["accent.r"], style_variant.values["accent.g"],
                                style_variant.values["accent.b"])
        style_variant.glow = (style_variant.values["glow_colour.r"], style_variant.values["glow_colour.g"],
                              style_variant.values["glow_colour.b"])
        result = to_image(compose(raw, style_variant, media, frames)).resize((tile_w, tile_h), Image.LANCZOS)
        column = index % columns
        row = index // columns
        x = 8 + column * (tile_w + 8)
        y = 8 + row * (tile_h + 22)
        sheet.paste(result, (x, y))
        draw_sheet.text((x + 6, y + tile_h + 4),
                        f"{section['meta'].get('name', '')} ({section['meta'].get('id', '')})",
                        fill=(220, 220, 226), font=small)
        index += 1
    sheet.save(output / "azy_mirror_themes.png")

    print(f"  wrote {output / 'azy_mirror_before_after.png'}")
    print(f"  wrote {output / 'azy_mirror_themes.png'} ({index} themes, {ids[0]} first)")
    print("  NOTE: a design preview of the maths, not a screenshot. Nothing here was run on Windows.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
