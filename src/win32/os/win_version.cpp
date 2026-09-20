#include "azy/win32/os/win_version.hpp"

#include <vector>

#include "azy/core/strings.hpp"
#include "azy/win32/os/win_api.hpp"
#include "azy/win32/os/win_util.hpp"

namespace azy {
namespace win {
namespace {

// Declared locally instead of pulling in <winternl.h>, which drags in a lot of
// kernel-only declarations we have no use for.
struct AzyOsVersionInfoW {
    ULONG dwOSVersionInfoSize;
    ULONG dwMajorVersion;
    ULONG dwMinorVersion;
    ULONG dwBuildNumber;
    ULONG dwPlatformId;
    WCHAR szCSDVersion[128];
};

int read_build_number() {
    // RtlGetVersion is the only version query that is not shimmed by the
    // application compatibility layer, so it reports the true build number.
    typedef LONG(WINAPI * RtlGetVersionFn)(AzyOsVersionInfoW*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        auto rtl_get_version = reinterpret_cast<RtlGetVersionFn>(
            reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
        if (rtl_get_version) {
            AzyOsVersionInfoW info{};
            info.dwOSVersionInfoSize = sizeof(info);
            if (rtl_get_version(&info) == 0 /*STATUS_SUCCESS*/) {
                return static_cast<int>(info.dwBuildNumber);
            }
        }
    }
    OSVERSIONINFOW fallback{};
    fallback.dwOSVersionInfoSize = sizeof(fallback);
#ifdef _MSC_VER
#pragma warning(suppress : 4996)  // GetVersionEx is deprecated but a valid fallback
#endif
    if (GetVersionExW(&fallback)) return static_cast<int>(fallback.dwBuildNumber);
    return 0;
}

std::string build_to_name(int build) {
    if (build >= 22000) return "Windows 11";
    if (build >= 10240) return "Windows 10";
    return "Windows";
}

std::string build_to_release(int build) {
    if (build >= 26100) return "24H2";
    if (build >= 22631) return "23H2";
    if (build >= 22621) return "22H2";
    if (build >= 22000) return "21H2 (11)";
    if (build >= 19045) return "22H2";
    if (build >= 19044) return "21H2";
    if (build >= 19043) return "21H1";
    if (build >= 19042) return "20H2";
    if (build >= 19041) return "2004";
    if (build >= 18363) return "1909";
    if (build >= 18362) return "1903";
    if (build >= 17763) return "1809";
    return std::string();
}

bool dwm_is_composing() {
    BOOL composition = FALSE;
    if (SUCCEEDED(DwmIsCompositionEnabled(&composition))) return composition != FALSE;
    return false;
}

// Probing by attempt: send each documented attribute to a throwaway window and
// see whether DWM accepts it. Cheaper and more reliable than build-number
// guessing, and it keeps working on future Windows builds.
struct ProbedCapabilities {
    bool dark_titlebar = false;
    bool frame_colors = false;
    bool rounded_corners = false;
    bool system_backdrop = false;
};

LRESULT CALLBACK ProbeWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

ProbedCapabilities probe_capabilities() {
    ProbedCapabilities result;

    static bool class_registered = false;
    const wchar_t* kClassName = L"AzySkin.HostProbe";
    HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!class_registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ProbeWndProc;
        wc.hInstance = instance;
        wc.lpszClassName = kClassName;
        if (!RegisterClassExW(&wc)) return result;
        class_registered = true;
    }

    // A never-shown popup is enough: DWM applies these attributes to the window
    // itself, no compositing of visible pixels required.
    HWND probe = CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, L"", WS_POPUP, 0, 0, 8, 8, nullptr, nullptr,
                                 instance, nullptr);
    if (!probe) return result;

    BOOL dark = TRUE;
    if (SUCCEEDED(DwmSetWindowAttribute(probe, dwm_attr::kUseImmersiveDarkMode, &dark, sizeof(dark))) ||
        SUCCEEDED(DwmSetWindowAttribute(probe, dwm_attr::kUseImmersiveDarkModeLegacy, &dark, sizeof(dark)))) {
        result.dark_titlebar = true;
    }

    const DWORD caption = dwm_attr::colorref(24, 24, 27);
    const DWORD border = dwm_attr::colorref(60, 60, 66);
    const DWORD text = dwm_attr::colorref(228, 229, 233);
    const bool caption_ok =
        SUCCEEDED(DwmSetWindowAttribute(probe, dwm_attr::kCaptionColor, &caption, sizeof(caption)));
    const bool border_ok =
        SUCCEEDED(DwmSetWindowAttribute(probe, dwm_attr::kBorderColor, &border, sizeof(border)));
    const bool text_ok = SUCCEEDED(DwmSetWindowAttribute(probe, dwm_attr::kTextColor, &text, sizeof(text)));
    result.frame_colors = caption_ok && border_ok && text_ok;

    const int round = dwm_attr::kCornerRoundSmall;
    if (SUCCEEDED(DwmSetWindowAttribute(probe, dwm_attr::kWindowCornerPreference, &round, sizeof(round)))) {
        result.rounded_corners = true;
    }

    const int backdrop = dwm_attr::kBackdropMica;
    if (SUCCEEDED(DwmSetWindowAttribute(probe, dwm_attr::kSystemBackdropType, &backdrop, sizeof(backdrop)))) {
        result.system_backdrop = true;
    }

    DestroyWindow(probe);
    return result;
}

HostInfo build_host_info() {
    HostInfo info;
    const int build = read_build_number();
    info.capabilities.windows_build = build;
    info.capabilities.dwm_composition = dwm_is_composing();

    // Capabilities are probed, never inferred from the build number: DWM
    // accepts or rejects each documented attribute for itself. Note these are
    // per-window attributes, so they work even when Windows itself is in light
    // mode - a light-mode user still gets a dark Premiere if they want one.
    const ProbedCapabilities probed = probe_capabilities();
    info.capabilities.dark_titlebar = probed.dark_titlebar;
    info.capabilities.frame_colors = probed.frame_colors;
    info.capabilities.rounded_corners = probed.rounded_corners;
    info.capabilities.system_backdrop = probed.system_backdrop;
    info.capabilities.layered_windows = true;
    info.capabilities.win_event_hooks = true;

    const std::string release = build_to_release(build);
    info.os_name = build_to_name(build);
    if (!release.empty()) info.os_name += " " + release;
    if (build > 0) info.os_name += str_format(" (build %d)", build);

    info.host_summary = str_format("%s; dwm=%d dark=%d colors=%d round=%d backdrop=%d light_apps=%d",
                                   info.os_name.c_str(),
                                   info.capabilities.dwm_composition ? 1 : 0,
                                   info.capabilities.dark_titlebar ? 1 : 0,
                                   info.capabilities.frame_colors ? 1 : 0,
                                   info.capabilities.rounded_corners ? 1 : 0,
                                   info.capabilities.system_backdrop ? 1 : 0,
                                   win::system_uses_dark_apps() ? 0 : 1);
    return info;
}

}  // namespace

int windows_build_number() { return host_info().capabilities.windows_build; }
std::string windows_version_string() { return host_info().os_name; }

const HostInfo& host_info() {
    static const HostInfo info = build_host_info();
    return info;
}

}  // namespace win
}  // namespace azy
