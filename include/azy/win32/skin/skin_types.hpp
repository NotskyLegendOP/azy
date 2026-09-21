// Azy Skin — Win32 layer: the data the composition manager works with.
//
// The rebuild reduced this to what the mirror actually needs: where Premiere is,
// what the user asked for, and what the engine is doing. The old per-layer fields
// (a DWM frame, four ring strips, a veil) are gone with the layers themselves.
#pragma once

#include <string>

#include "azy/core/geometry.hpp"
#include "azy/core/panel_map.hpp"
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
    bool visible = false;  // IsWindowVisible: the window is not hidden by its owner
    bool fullscreen = false;
    bool cloaked = false;
    bool foreground = false;

    std::wstring window_class;

    // A target is only usable when there is really something on screen to mirror:
    // a hidden window (Premiere keeps a few) would otherwise be captured off screen
    // while every status line happily reported "active".
    bool valid() const {
        return hwnd != nullptr && visible && !minimized && !cloaked && !visible_frame.empty();
    }
};

// Why the skin is (not) doing anything right now. Every skip reason is explicit so
// the tray tooltip and the log can explain themselves.
enum class SuspendReason {
    None = 0,
    SkinDisabled,     // master toggle off
    OriginalTheme,    // "Original" theme selected
    NoWindow,         // Premiere not running / no editor window yet
    Minimized,        // Premiere minimized (and the user asked us to suspend)
    Inactive,         // Premiere not in the foreground (opt-in)
    Hidden,           // the tracked window is hidden (not on screen at all)
    Dragging,         // user is inside a modal move/size loop
    Moving,           // user just moved/resized the window
    FullscreenTransition,
};

const char* suspend_reason_name(SuspendReason reason);

// The complete request the composition manager needs.
struct SkinRequest {
    SkinTarget target;
    bool skin_enabled = true;
    SuspendReason suspend = SuspendReason::NoWindow;
    // The theme and the sliders. This is the whole visual input: the renderer has no
    // colours of its own, they all come from the theme engine.
    Appearance appearance;
    bool performance_mode = false;
    // Debug mode (spec §38): the diagnostics report every fact the developer screen
    // asks for. It changes nothing about the skin itself.
    bool debug_mode = false;
    WorkspaceId workspace = WorkspaceId::Auto;
};

// What the engine actually did (used for logging, tray tooltip, diagnostics).
struct SkinState {
    // The mirror is the only visual layer: `mirror_active` means the skinned copy of
    // Premiere is on screen right now, which is exactly what "the skin is applied"
    // means since the rebuild.
    bool mirror_active = false;
    bool mirror_capturing = false;
    std::string mirror_state;  // the lifecycle state, verbatim
    std::string mirror_note;   // one line: what it is doing, or why it is not
    SuspendReason suspend = SuspendReason::None;
    unsigned long long applies = 0;   // number of applies
    unsigned long long failures = 0;  // number of failed operations (feeds safe mode)
    std::string last_error;
};

}  // namespace win
}  // namespace azy
