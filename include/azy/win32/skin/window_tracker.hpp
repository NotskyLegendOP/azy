// Azy Skin — Win32 layer: WindowTracker.
//
// Keeps one authoritative snapshot of the Premiere window: position, size, DPI,
// monitor, minimize/maximize/fullscreen state, foreground state. `refresh()` is
// only called when Windows has told us something actually changed, and it
// reports back whether the skin has to be re-applied — so a static Premiere
// window costs zero DWM calls and zero CPU.
#pragma once

#include <string>

#include "azy/win32/skin/skin_types.hpp"

namespace azy {
namespace win {

class WindowTracker {
public:
    struct RefreshResult {
        bool window_gone = false;   // the HWND no longer exists
        bool changed = false;       // anything the skin depends on changed
        bool needs_apply = false;   // DPI / monitor / state change: full re-apply
    };

    bool set_window(HWND hwnd, unsigned long pid);
    void clear();

    bool has_window() const { return target_.hwnd != nullptr; }
    HWND hwnd() const { return target_.hwnd; }
    unsigned long pid() const { return target_.pid; }
    unsigned long thread_id() const { return target_.thread_id; }
    const SkinTarget& target() const { return target_; }

    // Re-reads the window state. Cheap (a handful of Win32 calls) but called
    // only in response to events.
    RefreshResult refresh(double now);

    bool needs_apply() const { return needs_apply_; }
    void mark_applied();

    // Geometry changes are followed with a small hysteresis so dragging a window
    // does not cause one composition update per pixel.
    double seconds_since_geometry_change(double now) const;

    UINT last_applied_dpi() const { return applied_dpi_; }
    Rect last_applied_rect() const { return applied_visible_; }

private:
    SkinTarget target_;
    Rect applied_visible_;
    UINT applied_dpi_ = 0;
    bool has_applied_ = false;
    bool needs_apply_ = true;
    double last_geometry_change_ = 0.0;
    double last_window_count_check_ = 0.0;
    bool was_minimized_ = false;
    bool clamp_logged_ = false;  // the off-screen-frame explanation is logged once per window
};

}  // namespace win
}  // namespace azy
