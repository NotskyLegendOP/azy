#include "azy/win32/os/win_api.hpp"

namespace azy {
namespace win {
namespace {

Api g_api;
bool g_resolved = false;

template <typename T>
void bind(HMODULE module, const char* name, T& target) {
    if (module == nullptr) return;
    target = reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(module, name)));
}

void resolve() {
    if (g_resolved) return;
    g_resolved = true;

    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr) user32 = LoadLibraryW(L"user32.dll");
    HMODULE shcore = GetModuleHandleW(L"shcore.dll");
    if (shcore == nullptr) shcore = LoadLibraryW(L"shcore.dll");

    bind(user32, "GetDpiForWindow", g_api.get_dpi_for_window);
    bind(user32, "GetDpiForSystem", g_api.get_dpi_for_system);
    bind(user32, "SetProcessDpiAwarenessContext", g_api.set_process_dpi_awareness_context);
    bind(user32, "GetSystemMetricsForDpi", g_api.get_system_metrics_for_dpi);
    bind(user32, "AdjustWindowRectExForDpi", g_api.adjust_window_rect_ex_for_dpi);
    bind(user32, "EnableNonClientDpiScaling", g_api.enable_non_client_dpi_scaling);
    bind(shcore, "GetDpiForMonitor", g_api.get_dpi_for_monitor);
    bind(shcore, "SetProcessDpiAwareness", g_api.shcore_set_process_dpi_awareness);
    g_api.monitor_dpi_api = g_api.get_dpi_for_monitor != nullptr;
}

}  // namespace

const Api& api() {
    resolve();
    return g_api;
}

UINT dpi_for_window(HWND hwnd) {
    const Api& a = api();
    if (a.get_dpi_for_window && hwnd) {
        const UINT dpi = a.get_dpi_for_window(hwnd);
        if (dpi != 0) return dpi;
    }
    if (hwnd) {
        RECT rect{};
        if (GetWindowRect(hwnd, &rect)) return dpi_for_rect(rect);
    }
    return system_dpi();
}

UINT dpi_for_rect(const RECT& rect) {
    const Api& a = api();
    HMONITOR monitor = MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);
    if (a.shcore_set_process_dpi_awareness == nullptr) {
        // shcore absent (Windows 8.1+ always has it; this is belt and braces).
    }
    if (a.get_dpi_for_monitor && monitor) {
        UINT x = 0, y = 0;
        if (SUCCEEDED(a.get_dpi_for_monitor(monitor, static_cast<int>(MDT_EFFECTIVE_DPI), &x, &y)) && x != 0) {
            return x;
        }
    }
    return system_dpi();
}

UINT system_dpi() {
    const Api& a = api();
    if (a.get_dpi_for_system) {
        const UINT dpi = a.get_dpi_for_system();
        if (dpi != 0) return dpi;
    }
    HDC screen = GetDC(nullptr);
    if (screen) {
        const int dpi = GetDeviceCaps(screen, LOGPIXELSX);
        ReleaseDC(nullptr, screen);
        if (dpi > 0) return static_cast<UINT>(dpi);
    }
    return 96;
}

bool process_is_per_monitor_aware() {
    // Ask Windows for the effective awareness of this process. The value is a
    // pseudo-handle we only compare by identity, so no extra types are needed.
    typedef HANDLE(WINAPI * GetThreadDpiAwarenessContextFn)(void);
    typedef int(WINAPI * GetAwarenessFromDpiAwarenessContextFn)(HANDLE);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return false;
    auto get_ctx = reinterpret_cast<GetThreadDpiAwarenessContextFn>(
        reinterpret_cast<void*>(GetProcAddress(user32, "GetThreadDpiAwarenessContext")));
    auto get_awareness = reinterpret_cast<GetAwarenessFromDpiAwarenessContextFn>(
        reinterpret_cast<void*>(GetProcAddress(user32, "GetAwarenessFromDpiAwarenessContext")));
    if (!get_ctx || !get_awareness) return false;
    const int awareness = get_awareness(get_ctx());
    // DPI_AWARENESS_PER_MONITOR_AWARE == 2
    return awareness == 2;
}

}  // namespace win
}  // namespace azy
