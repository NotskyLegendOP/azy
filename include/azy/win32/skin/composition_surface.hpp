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

// What the last present actually produced. Exposed so the settings window can
// answer "is the ring on screen, and if not, why not" from the machine itself
// instead of from a log file.
struct RingReport {
    bool presented = false;      // the strips are on screen right now
    Rect frame;                  // the rectangle the ring was drawn on
    int thickness_px = 0;        // strip thickness (band + the two 1px strokes)
    int band_px = 0;
    int radius_px = 0;
    size_t bitmap_bytes = 0;
    unsigned char max_alpha = 0; // 0 would mean an invisible bitmap
    bool above = false;          // strips sit in front of the target window
    int misplaced_strips = 0;    // strips Windows did not leave where they were placed
    std::string error;           // last failure, empty when the last present worked
};

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

    // Puts the strips back directly above `below` without repainting them. Something
    // raising Premiere (activating it is enough - Azy's strips are ordinary windows)
    // leaves the ring *behind* the window it decorates, where it is invisible: every
    // call still succeeds and nothing reports a problem. Returns true when the strips
    // end up in front of `below`.
    bool reposition(HWND below);

    void hide();
    void destroy();

    // Asks the desktop itself whether the ring is on screen: samples a few pixels
    // inside the top strip with the ring hidden and with it shown, and compares
    // them. Every call Azy makes can return success while the user sees nothing,
    // so this is the only check that can answer "is it really there".
    // The ring is only hidden for a few milliseconds (DWM flush between samples),
    // never while the user could notice, and `detail` always describes what was
    // measured, pass or fail.
    bool probe_visible(std::string* detail);

    bool visible() const { return visible_; }
    const RingReport& report() const { return report_; }
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
    // Shows the strips that already exist (SW_SHOWNA: never activates them), used
    // by probe_visible to put the ring back after hiding it for one frame.
    void show_strips();

    Strip strips_[kStripCount];
    std::wstring class_name_;
    bool class_registered_ = false;
    bool visible_ = false;
    unsigned long long presents_ = 0;
    RingReport report_;
};

}  // namespace win
}  // namespace azy
