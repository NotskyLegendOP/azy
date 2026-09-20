// Azy Skin — Win32 layer: the composition surfaces.
//
// Level 2 of the visual strategy: click-through, non-activating, layered
// top-level windows that carry Azy's ring. They are created once, moved with
// SetWindowPos and painted with UpdateLayeredWindow — the documented, supported
// way for one process to draw a translucent surface over another process' window
// without injecting anything into it.
//
// The ring is split into four thin strips (top, bottom, left, right) rather than
// one window-sized layer:
//
//   * memory: ~1 MB at 4K/200% instead of ~33 MB for a full-frame bitmap
//   * per-frame compositing: DWM only blends the thin strips
//   * and the strips are still pixel-exact, because each one is drawn in frame
//     coordinates with a 1px stroke — never scaled or stretched
//
// The corner arcs live in the horizontal strips, so nothing is drawn twice.
// They are never larger than the Premiere window and are hidden (not merely
// transparent) whenever the skin is suspended.
#pragma once

#include <string>
#include <cstddef>

#include "azy/core/geometry.hpp"
#include "azy/win32/os/win_compat.hpp"
#include "azy/win32/skin/gdiplus_renderer.hpp"

namespace azy {
namespace win {

class CompositionSurface {
public:
    CompositionSurface() = default;
    ~CompositionSurface() { destroy(); }

    CompositionSurface(const CompositionSurface&) = delete;
    CompositionSurface& operator=(const CompositionSurface&) = delete;

    // Paints and positions the ring around `frame`. `below` is the Premiere
    // window: each strip is inserted directly above it in the z-order, so the
    // ring follows Premiere's own stacking and never covers an unrelated
    // application.
    bool present(HWND below, const Rect& frame, const RingVisual& visual, std::string* error);

    void hide();
    void destroy();

    bool visible() const { return visible_; }
    // Primary (top) strip: used for diagnostics and identity checks.
    HWND hwnd() const { return strips_[kTop].hwnd; }
    int strip_count() const { return kStripCount; }
    unsigned long long presents() const { return presents_; }
    // Total bitmap memory currently held by the surfaces, in bytes.
    size_t bitmap_bytes() const;

private:
    enum StripIndex { kTop = 0, kBottom, kLeft, kRight, kStripCount };

    struct Strip {
        HWND hwnd = nullptr;
        GdiPlusRenderer renderer;
        Rect rect;
    };

    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    bool ensure_created(std::string* error);
    bool register_class(std::string* error);
    bool present_strip(int index, HWND below, const Rect& frame, const RingVisual& visual, std::string* error);

    Strip strips_[kStripCount];
    std::wstring class_name_;
    bool class_registered_ = false;
    bool visible_ = false;
    unsigned long long presents_ = 0;
};

}  // namespace win
}  // namespace azy
