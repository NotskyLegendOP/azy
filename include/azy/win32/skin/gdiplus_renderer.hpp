// Azy Skin — Win32 layer: the (only) renderer.
//
// Scope check: Azy draws exactly one thing — a thin, rounded, softly shaded ring
// that sits on the inside edge of the Premiere window. It is painted into a
// premultiplied 32-bit ARGB bitmap with GDI+ and handed to Windows through
// UpdateLayeredWindow. There is no animation, no timer, no GPU work: one paint
// per geometry/appearance change, and the bitmap is cached in between.
#pragma once

#include <string>

#include "azy/core/geometry.hpp"
#include "azy/core/theme.hpp"
#include "azy/win32/os/win_compat.hpp"

namespace azy {
namespace win {

// Everything needed to paint one ring.
struct RingVisual {
    int width_px = 0;    // band thickness (physical px)
    int radius_px = 0;   // corner radius matching the window frame (physical px)
    bool draw_fill = true;
    bool draw_shadow = true;
    bool draw_highlight = true;
    Rgba fill;
    Rgba border;
    Rgba highlight;
    Rgba shadow;
};

// GDI+ process-wide session (startup/shutdown).
class GdiPlusSession {
public:
    static bool start(std::string* error);
    static void stop();
    static bool active();
    static std::string version_string();
};

class GdiPlusRenderer {
public:
    ~GdiPlusRenderer() { release(); }

    // Paints `visual` into the internal bitmap, resizing it when needed.
    // Returns false with a reason when GDI+ refuses (which counts as a failure
    // towards Safe Mode).
    bool render(const RingVisual& visual, std::string* error);

    bool valid() const { return bitmap_ != nullptr && width_ > 0 && height_ > 0; }
    int width() const { return width_; }
    int height() const { return height_; }
    HDC memory_dc() const { return memory_dc_; }
    HBITMAP bitmap() const { return bitmap_; }
    POINT origin() const { return POINT{0, 0}; }
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
    std::string last_error_;
};

}  // namespace win
}  // namespace azy
