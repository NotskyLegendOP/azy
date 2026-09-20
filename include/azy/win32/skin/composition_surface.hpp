// Azy Skin — Win32 layer: the composition surface.
//
// Level 2 of the visual strategy: one click-through, non-activating, layered
// top-level window that carries Azy's ring. It is created once, moved/resized
// with SetWindowPos and painted with UpdateLayeredWindow — the documented,
// supported way for one process to draw a translucent surface over another
// process' window without injecting anything into it.
//
// It is never larger than the Premiere window it decorates and it is hidden
// (not just transparent) whenever the skin is suspended, so nothing is being
// composited while the user is editing.
#pragma once

#include <string>

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

    // Creates the window if needed. Returns false (with a reason) when Windows
    // refuses, in which case the skin continues with DWM-only treatment.
    bool ensure_created(std::string* error);

    // Paints and positions the ring. `below` is the Premiere window: the surface
    // is inserted directly above it in the z-order, so it follows Premiere's own
    // stacking instead of floating over unrelated applications. Pass nullptr to
    // fall back to topmost.
    bool present(HWND below, const Rect& screen_rect, const RingVisual& visual, std::string* error);

    void hide();
    void destroy();

    bool visible() const { return visible_; }
    HWND hwnd() const { return hwnd_; }
    const Rect& last_rect() const { return last_rect_; }
    unsigned long long presents() const { return presents_; }

private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    bool register_class(std::string* error);

    HWND hwnd_ = nullptr;
    std::wstring class_name_;
    bool class_registered_ = false;
    bool visible_ = false;
    Rect last_rect_;
    GdiPlusRenderer renderer_;
    unsigned long long presents_ = 0;
};

}  // namespace win
}  // namespace azy
