// Azy Skin — Win32 layer: the (only) renderer.
//
// Scope check: Azy draws exactly one thing — a thin vignette ring that sits on
// the inside edge of the Premiere window: a 1px hairline border, a per-pixel
// falloff (the "glass wash" and the "inner shadow" in one pass), and a barely
// visible top highlight. It is painted into a premultiplied 32-bit ARGB bitmap
// with GDI+ and handed to Windows through UpdateLayeredWindow.
//
// There is no animation, no timer and no per-frame work: one paint per
// geometry/appearance change, and the bitmap is cached in between.
#pragma once

#include <string>

#include "azy/core/geometry.hpp"
#include "azy/core/theme.hpp"
#include "azy/win32/os/win_compat.hpp"

namespace azy {
namespace win {

// Everything needed to paint one ring, or one strip of one ring.
//
// The ring is drawn in *frame* coordinates and then offset into the target
// bitmap, which is how four thin strips can together cover the frame edge without
// seams or resampling.
struct RingVisual {
    int width_px = 0;   // full frame width (not the bitmap's width)
    int height_px = 0;  // full frame height
    int band_px = 1;    // how far the treatment reaches inward from the edge
    int radius_px = 0;  // corner radius (0 = square)

    int origin_x = 0;   // where the target bitmap starts, in frame coordinates
    int origin_y = 0;

    bool draw_fill = true;       // glass wash at the very edge
    bool draw_shadow = true;     // soft inner shadow falloff across the band
    bool draw_highlight = true;  // 1px top inner highlight
    bool draw_border = true;     // 1px hairline border on the frame edge

    Rgba bezel;      // 1px raised edge, immediately inside the hairline
    Rgba fill;       // wash colour, alpha = strength at the edge
    Rgba border;     // hairline colour
    Rgba highlight;  // top highlight colour
    Rgba shadow;     // inner shadow colour, alpha = strength at the edge
};

// GDI+ process-wide session (startup/shutdown).
class GdiPlusSession {
public:
    static bool start(std::string* error);
    static void stop();
    static bool active();
};

class GdiPlusRenderer {
public:
    ~GdiPlusRenderer() { release(); }

    // Allocates (or reuses) the target bitmap. The caller decides the size,
    // because a strip is not the whole frame.
    bool prepare(int width, int height, std::string* error);

    // Paints `visual` into the prepared bitmap. Returns false with a reason when
    // GDI+ refuses (which counts as a failure towards Safe Mode).
    bool render(const RingVisual& visual, std::string* error);

    bool valid() const { return bitmap_ != nullptr && width_ > 0 && height_ > 0; }
    int width() const { return width_; }
    int height() const { return height_; }
    // Strongest alpha currently in the bitmap (0 = nothing would be visible).
    // A cheap self-check: it tells "the ring is drawn but invisible" apart from
    // "the ring was never placed" without capturing the screen.
    unsigned char max_alpha() const;
    HDC memory_dc() const { return memory_dc_; }
    HBITMAP bitmap() const { return bitmap_; }
    SIZE size() const { return SIZE{width_, height_}; }

    void release();

private:
    bool ensure_size(int width, int height, std::string* error);

    HDC memory_dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    void* bits_ = nullptr;
    void* graphics_bitmap_ = nullptr;  // Gdiplus::Bitmap*, hidden from the header
    int width_ = 0;
    int height_ = 0;
};

}  // namespace win
}  // namespace azy
