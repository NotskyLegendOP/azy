// Azy Skin — Win32 layer: the data the skin engine works with.
//
// Kept free of rendering details so both composition levels (DWM window
// attributes and Azy's own click-through surface) can consume the same struct.
#pragma once

#include <string>

#include "azy/core/compat.hpp"
#include "azy/core/geometry.hpp"
#include "azy/core/theme.hpp"
#include "azy/win32/os/win_compat.hpp"

namespace azy {
namespace win {

// A snapshot of the Premiere window the skin is following.
struct SkinTarget {
    HWND hwnd = nullptr;
    unsigned long pid = 0;
    unsigned long thread_id = 0;

    Rect frame;          // GetWindowRect (includes any invisible resize border)
    Rect visible_frame;  // DWM extended frame bounds: what the user actually sees
    Rect monitor;        // monitor rectangle (physical pixels)
    Rect work_area;      // monitor work area (excludes the taskbar)

    UINT dpi = 96;
    bool minimized = false;
    bool maximized = false;
    bool fullscreen = false;
    bool cloaked = false;
    bool foreground = false;
    int window_count = 0;  // visible top-level windows of the process

    std::wstring window_class;

    bool valid() const { return hwnd != nullptr && !minimized && !cloaked && !visible_frame.empty(); }

    // Corner radius actually usable for this window: never larger than 20% of
    // the shorter side (so a small floating dialog cannot become a pill), and
    // zero when the window fills the monitor (rounding a fullscreen window would
    // clip its corners for no visual gain).
    int effective_radius_dip(int requested_dip) const {
        if (maximized || fullscreen) return 0;
        const int shorter = visible_frame.width() < visible_frame.height() ? visible_frame.width()
                                                                          : visible_frame.height();
        const int cap_px = static_cast<int>(shorter * 0.2);
        const int requested_px = dip_to_px(requested_dip, dpi);
        const int applied = requested_px < cap_px ? requested_px : cap_px;
        return applied <= 0 ? 0 : static_cast<int>(px_to_dip(applied, dpi) + 0.5);
    }
};

// Why the skin is (not) doing anything right now. Every skip reason is explicit
// so the tray tooltip and the log can explain themselves.
enum class SuspendReason {
    None = 0,
    SkinDisabled,     // master toggle off
    OriginalTheme,    // "Original" theme selected
    NoWindow,         // Premiere not running / no editor window yet
    Minimized,        // Premiere minimized (and the user asked us to suspend)
    Inactive,         // Premiere not in the foreground (opt-in)
    Dragging,         // user is inside a modal move/size loop
    Moving,           // user just moved/resized the window
    FullscreenTransition,
};

const char* suspend_reason_name(SuspendReason reason);

// The complete request the engine needs: target + policy + resolved visuals.
struct SkinRequest {
    SkinTarget target;
    bool skin_enabled = true;
    SuspendReason suspend = SuspendReason::NoWindow;
    FeatureSet features;
    ThemePalette palette;
    bool performance_mode = false;
    bool experimental = false;
};

// What the engine actually did (used for logging, tray tooltip, diagnostics).
struct SkinState {
    bool frame_applied = false;    // DWM window attributes are in place
    bool surface_visible = false;  // Azy's composition surface is on screen
    SuspendReason suspend = SuspendReason::None;
    unsigned long long applies = 0;   // number of successful applies
    unsigned long long failures = 0;  // number of failed operations (feeds safe mode)
    std::string last_error;
};

}  // namespace win
}  // namespace azy
