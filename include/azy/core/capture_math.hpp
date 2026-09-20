// Azy Skin — portable core: the arithmetic behind the captured-image overlay.
//
// The duplicate overlay draws a captured image of Premiere's window into a window
// of Azy's own, positioned exactly over it. Two pieces of maths decide whether
// that lands correctly, and neither of them is allowed to live in the Windows
// layer where it cannot be tested:
//
//  1. Which part of the captured texture corresponds to Azy's window. The capture
//     is the whole window (including the caption and the invisible resize border,
//     which for a maximized window hangs off the edge of the display); Azy's
//     window covers only the *visible* part of it. The sub-rectangle is expressed
//     as fractions of the captured rectangle rather than pixels, which is what
//     makes it independent of DPI and of any difference between the texture size
//     and the window size.
//
//  2. Which parts of that image are treated as "video" and left alone. The
//     Program and Source monitors must not be darkened: a dark filter over the
//     footage is the one thing this feature must never do. The panel model already
//     knows where they are, so the shader gets them as pass-through rectangles.
#pragma once

#include <cstddef>
#include <vector>

#include "azy/core/geometry.hpp"
#include "azy/core/panel_map.hpp"

namespace azy {

// The sub-rectangle of a texture that maps to a window: normalized 0..1, source
// coordinates (0,0 = top-left of the captured image).
struct UvRect {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
    bool valid = false;

    float width() const { return u1 - u0; }
    float height() const { return v1 - v0; }
};

// Maps `overlay` (the rectangle Azy's window covers, in screen pixels) into
// `captured` (the rectangle the capture contains, in screen pixels).
//
// `captured` empty or degenerate, or an overlay that does not intersect it at all,
// returns an invalid rect: the caller then skips the capture rather than drawing a
// stretched or mirrored image.
UvRect map_overlay_to_capture(const Rect& overlay, const Rect& captured);

// A rectangle of the display left untouched by the skin (pass-through), in
// window-local pixels.
struct LocalRect {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    bool empty() const { return right <= left || bottom <= top; }
};

// Converts a rectangle expressed in screen coordinates into window-local pixels
// (origin at the overlay's own top-left), clipping it to the overlay and dropping
// rectangles that end up degenerate.
//
// `source` and `overlay` are in screen coordinates; the result is relative to
// `overlay`. A rectangle fully outside returns an empty LocalRect.
LocalRect clip_to_overlay(const Rect& source, const Rect& overlay);

// The monitors inside a panel map, converted for the shader: the regions whose
// pixels must survive unchanged.
//
// Ordering is Program Monitor, Source Monitor, then any other panel that carries
// picture content, so a shader with a small fixed array always gets the two that
// matter most first.
std::vector<LocalRect> monitor_pass_through(const std::vector<PanelRect>& panels, const Rect& overlay,
                                            const Rect& client_origin);

// The panel borders to draw, converted for the shader, most useful first (the
// menu bar, the header, the timeline, the docks), clipped and de-duplicated
// against `exclude` (the pass-through regions, whose borders must not be drawn
// over the video).
std::vector<LocalRect> panel_hairlines(const std::vector<PanelRect>& panels, const Rect& overlay,
                                       const Rect& client_origin, const std::vector<LocalRect>& exclude,
                                       std::size_t limit);

// The rectangle the duplicate window should cover.
//
// A maximized window's frame is deliberately reported by Windows as larger than
// the monitor (the resize border hangs off the edge), so the visible result on
// screen is the work area, not the frame. A fullscreen window has no taskbar to
// avoid, so its monitor bounds are the truth. Everything else is the visible
// frame. The result is intersected with the window that is really visible, which
// keeps a stale rectangle from placing the duplicate somewhere the user is not
// looking.
Rect overlay_rect(const Rect& visible_frame, const Rect& monitor, const Rect& work_area, bool maximized,
                  bool fullscreen);

// Packs rectangles into the flat float array a constant buffer expects:
// [left, top, right, bottom] per rectangle, padded with zeros up to `slots`.
void pack_rects(const std::vector<LocalRect>& rects, float* out, std::size_t slots);

}  // namespace azy
