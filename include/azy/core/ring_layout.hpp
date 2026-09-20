// Azy Skin — portable core: the geometry of the four-strip ring.
//
// The visual ring is not one window covering Premiere: it is four thin strips
// (top, bottom, left, right) that follow the window's visible frame. Splitting it
// keeps the bitmaps small (~78 KB on a 1080p window instead of a 33 MB
// window-sized ARGB layer) and lets a 1px line stay exactly one pixel, because
// every strip is drawn in frame coordinates rather than scaled.
//
// The arithmetic lives here - with no Windows dependency - so it can be unit
// tested: overlapping strips would double-paint a corner, a gap would leave a
// visible notch, and a ring that is too thick for a small window would swallow the
// window it is supposed to decorate.
#pragma once

#include "azy/core/geometry.hpp"

namespace azy {

// Thickness of one strip, in physical pixels: enough to hold the treated band plus
// the two 1px strokes (the hairline on the frame edge and the bezel just inside
// it), and enough to hold the corner arc.
inline int ring_strip_thickness(int band_px, int radius_px) {
    const int with_strokes = band_px + 2;
    const int with_arc = radius_px + 1;
    return with_strokes > with_arc ? with_strokes : with_arc;
}

struct RingGeometry {
    int thickness_px = 0;
    int radius_px = 0;
    // False when the window is too small for a ring to make sense (Premiere's
    // internal helper windows, for example). Callers then simply draw nothing.
    bool valid = false;
};

// Works out the ring for one frame:
//
//  * the thickness is capped at a twelfth of the shorter side, so a small floating
//    panel gets a proportionally small ring rather than four thick strips that meet
//    in the middle;
//  * the radius is capped at thickness - 1 so the corner arc always fits inside the
//    strip that has to paint it;
//  * a window with less than 8px of content left over gets no ring at all.
inline RingGeometry ring_geometry(const Rect& frame, int band_px, int radius_px) {
    RingGeometry geometry;
    const int width = frame.width();
    const int height = frame.height();
    const int shorter = width < height ? width : height;
    if (shorter <= 0) return geometry;

    int thickness = ring_strip_thickness(band_px, radius_px);
    const int proportional = shorter / 12;
    const int cap = proportional > 3 ? proportional : 3;
    if (thickness > cap) thickness = cap;

    if (shorter < 2 * thickness + 8) return geometry;

    geometry.thickness_px = thickness;
    geometry.radius_px = radius_px < thickness ? radius_px : thickness - 1;
    geometry.valid = true;
    return geometry;
}

struct RingStrips {
    Rect top;
    Rect bottom;
    Rect left;
    Rect right;
    bool valid = false;
};

// The rectangle the ring is drawn on, given what Windows reports for a window.
//
// A maximized window is deliberately placed *off* the display: its invisible
// resize border hangs over the monitor edges, so GetWindowRect - and, depending
// on the Windows build, the DWM extended frame bounds - can start at a negative
// coordinate or end past the screen. The ring is drawn inside the frame edge, so
// using those numbers puts the entire treatment outside the visible desktop: the
// skin runs, every call succeeds, and the user sees nothing at all.
//
// What the user actually sees on a maximized window is the monitor's work area;
// on a fullscreen window it is the whole monitor. Normal windows keep exactly the
// bounds they report, because there the reported frame *is* what is on screen.
inline Rect ring_frame(const Rect& visible_frame, const Rect& monitor, const Rect& work_area,
                       bool maximized, bool fullscreen) {
    if (maximized && !work_area.empty()) return work_area;
    if (fullscreen && !monitor.empty()) return monitor;
    if (!monitor.empty()) {
        // A window whose frame sticks out over the display is not a window anyone
        // can see the outside of: draw on the part that is really on screen. This
        // is what covers "borderless fullscreen" windows that Windows does not
        // report as maximized (Premiere's fullscreen mode, for one), and windows
        // dragged half off the edge.
        const Rect clamped = intersect_rect(visible_frame, monitor);
        if (!clamped.empty()) return clamped;
    }
    return visible_frame;
}

// Splits the border band of `frame` into four strips of `thickness` pixels.
//
// The horizontal strips keep the full width - they own the corner arcs - and the
// vertical strips are inset by the thickness, so no pixel belongs to two strips.
inline RingStrips ring_strip_rects(const Rect& frame, int thickness) {
    RingStrips strips;

    int t = thickness;
    if (t < 1) t = 1;

    // Two strips plus at least one pixel of window in between.
    if (frame.width() < 2 * t + 1 || frame.height() < 2 * t + 1) return strips;

    strips.top = Rect::from_size(frame.left, frame.top, frame.width(), t);
    strips.bottom = Rect::from_size(frame.left, frame.bottom - t, frame.width(), t);
    strips.left = Rect::from_size(frame.left, frame.top + t, t, frame.height() - 2 * t);
    strips.right = Rect::from_size(frame.right - t, frame.top + t, t, frame.height() - 2 * t);
    strips.valid = true;
    return strips;
}

}  // namespace azy
