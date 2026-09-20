// Azy Skin — Win32 layer: lazily resolved optional OS entry points.
//
// Everything that only exists on newer Windows (per-monitor-v2 DPI APIs, the
// ForDpi metric helpers, Shcore's GetDpiForMonitor) is resolved at runtime.
// Missing entry points degrade to the older equivalent rather than failing to
// start, and the source no longer depends on how new the build SDK is.
#pragma once

#include "azy/win32/os/win_compat.hpp"

namespace azy {
namespace win {

struct Api {
    // user32.dll
    UINT(WINAPI* get_dpi_for_window)(HWND) = nullptr;
    UINT(WINAPI* get_dpi_for_system)() = nullptr;
    BOOL(WINAPI* set_process_dpi_awareness_context)(HANDLE) = nullptr;
    int(WINAPI* get_system_metrics_for_dpi)(int, UINT) = nullptr;
    BOOL(WINAPI* adjust_window_rect_ex_for_dpi)(LPRECT, DWORD, BOOL, DWORD, UINT) = nullptr;
    BOOL(WINAPI* enable_non_client_dpi_scaling)(HWND) = nullptr;
    BOOL(WINAPI* set_process_dpi_awareness)(void) = nullptr;  // (shcore, resolved separately)

    // shcore.dll
    HRESULT(WINAPI* get_dpi_for_monitor)(HMONITOR, int, UINT*, UINT*) = nullptr;
    HRESULT(WINAPI* shcore_set_process_dpi_awareness)(int) = nullptr;  // PROCESS_DPI_AWARENESS

    // user32.dll — child window enumeration (pre-Win7 behaviour lives in
    // EnumChildWindows, which is always available; this is the recursive-safe
    // documented variant added for 64-bit safety).
    BOOL(WINAPI* is_window_visible)(HWND) = nullptr;  // not optional, kept for symmetry

    bool per_monitor_dpi_v2 = false;  // set when SetProcessDpiAwarenessContext succeeded
    bool monitor_dpi_api = false;     // GetDpiForMonitor available (Shcore)
};

// Resolves once, thread-safely. Never throws.
const Api& api();

// Effective DPI of the monitor a window/rectangle is on, with graceful
// fallbacks: GetDpiForWindow -> GetDpiForMonitor -> system DPI -> 96.
UINT dpi_for_window(HWND hwnd);
UINT dpi_for_rect(const RECT& rect);
UINT system_dpi();

// True when the whole process is already per-monitor DPI aware (set by the
// embedded application manifest). Used for logging + as a guard so the runtime
// fallback below never fights the manifest.
bool process_is_per_monitor_aware();

}  // namespace win
}  // namespace azy
