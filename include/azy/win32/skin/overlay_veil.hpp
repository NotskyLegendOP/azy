// Azy Skin — Win32 layer: the whole-window overlay (the "veil").
//
// The ring decorates the *edge* of Premiere's window; the veil covers everything
// inside that edge, which is what makes the application read as skinned rather
// than outlined. It is one click-through, non-activating, layered top-level window
// the size of the tracked window, filled with a single translucent colour and
// blended by Windows with a constant alpha (SetLayeredWindowAttributes).
//
// Why a constant alpha instead of an ARGB bitmap: a full-window ARGB surface costs
// 8 MB at 1080p and 33 MB at 4K inside Azy's own address space, and DWM would blend
// every pixel of it on every change. A constant-alpha layer is one colour, no
// bitmap, no per-pixel work and nothing to animate - the cheapest documented way to
// tint a whole window. It is still a real window composition, so the same rules as
// the ring apply: click-through, never activated, never in Alt+Tab, hidden the
// instant the skin is suspended, and destroyed with the process.
//
// Stacking: the veil sits directly above Premiere and directly *below* Azy's ring,
// so the 1px hairline and the bezel stay crisp on top of the tint instead of being
// dimmed by it.
#pragma once

#include <string>

#include "azy/core/geometry.hpp"
#include "azy/core/theme.hpp"
#include "azy/win32/os/win_compat.hpp"

namespace azy {
namespace win {

class OverlayVeil {
public:
    OverlayVeil() = default;
    ~OverlayVeil() { destroy(); }

    OverlayVeil(const OverlayVeil&) = delete;
    OverlayVeil& operator=(const OverlayVeil&) = delete;

    // Covers `frame` with `color` (its alpha is the strength). The veil is placed
    // directly above `below` (Premiere) unless `ring_strip` is a live window, in
    // which case it is placed directly below that instead - keeping the ring on top.
    bool present(HWND below, HWND ring_strip, const Rect& frame, const Rgba& color, std::string* error);

    void hide();
    void destroy();

    bool visible() const { return visible_; }
    // The veil window: used as the z-order anchor of the ring's strips.
    HWND hwnd() const { return hwnd_; }
    unsigned char alpha() const { return color_.a; }
    Rgba color() const { return color_; }

private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    bool ensure_created(std::string* error);
    void apply_color();

    HWND hwnd_ = nullptr;
    std::wstring class_name_;
    Rgba color_{0, 0, 0, 0};
    Rect rect_;
    bool painted_ = false;
    bool visible_ = false;
};

}  // namespace win
}  // namespace azy
