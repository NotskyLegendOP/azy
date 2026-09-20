// Azy Skin — Win32 layer: SDK-version shims.
//
// Azy Skin supports Windows 10 1809 .. Windows 11, compiled against whatever
// Windows SDK the builder has. Newer DWM attributes are therefore not included
// from the SDK headers but declared here with their documented numeric IDs, so
// the same source compiles on an old and a new SDK.
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef WINVER
#define WINVER 0x0A00  // Windows 10
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <dwmapi.h>
#include <windows.h>

namespace azy {
namespace dwm_attr {

// Documented DwmSetWindowAttribute integer attributes (dwamapi.h in the
// Windows 11 SDK; safe to send to older DWM, which rejects unknown IDs).
constexpr DWORD kUseImmersiveDarkMode = 20;   // BOOL  (Attrs 19 = pre-20H1 spelling)
constexpr DWORD kUseImmersiveDarkModeLegacy = 19;
constexpr DWORD kWindowCornerPreference = 33; // DWM_WINDOW_CORNER_PREFERENCE
constexpr DWORD kBorderColor = 34;            // COLORREF
constexpr DWORD kCaptionColor = 35;           // COLORREF
constexpr DWORD kTextColor = 36;              // COLORREF
constexpr DWORD kSystemBackdropType = 38;     // DWM_SYSTEMBACKDROP_TYPE

// DWMWA_EXTENDED_FRAME_BOUNDS (9): the *visible* window frame. On Windows 10+
// GetWindowRect is inflated by the invisible resize border, so anything that has
// to line up with what the user sees must use this instead.
constexpr DWORD kExtendedFrameBounds = 9;
constexpr DWORD kUseHostBackdropBrush = 17;  // used to verify composition support

constexpr int kCornerDefault = 0;
constexpr int kCornerDoNotRound = 1;
constexpr int kCornerRound = 2;
constexpr int kCornerRoundSmall = 3;

constexpr int kBackdropAuto = 0;
constexpr int kBackdropNone = 1;
constexpr int kBackdropMica = 2;
constexpr int kBackdropAcrylic = 3;  // "Transient window" backdrop

// COLORREF (0x00BBGGRR) from 8-bit components.
constexpr DWORD colorref(unsigned char r, unsigned char g, unsigned char b) {
    return static_cast<DWORD>(r) | (static_cast<DWORD>(g) << 8) | (static_cast<DWORD>(b) << 16);
}

}  // namespace dwm_attr

// WS_EX_NOREDIRECTIONBITMAP is available since Windows 8; guarded for old SDKs.
#ifndef WS_EX_NOREDIRECTIONBITMAP
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
#endif

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
// Per-monitor-v2 awareness context pseudo-handle (-4). Declared as HANDLE so it
// matches the documented signature on every SDK.
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)(LONG_PTR)-4)
#endif

#ifndef MDT_EFFECTIVE_DPI
#define MDT_EFFECTIVE_DPI 0
#endif

// Window messages used for system theme/DPI change notifications.
#ifndef WM_DPICHANGED_BEFOREPARENT
#define WM_DPICHANGED_BEFOREPARENT 0x02E2
#endif

}  // namespace azy
