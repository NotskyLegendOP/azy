#include "azy/win32/skin/composition_surface.hpp"

#include "azy/core/log.hpp"
#include "azy/core/strings.hpp"
#include "azy/win32/skin/input_guard.hpp"
#include "azy/win32/os/win_util.hpp"

namespace azy {
namespace win {

LRESULT CALLBACK CompositionSurface::window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        // Should never be reached (WS_EX_TRANSPARENT answers hit tests before the
        // window procedure is called), but if it ever is, pass the point to the
        // window below: Premiere keeps every click.
        case WM_NCHITTEST:
            input_guard::note_hit_test();
            return HTTRANSPARENT;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_ERASEBKGND:
            return 1;  // the layered surface is fully painted by UpdateLayeredWindow
        case WM_NCACTIVATE:
        case WM_SETFOCUS:
        case WM_ACTIVATE:
            return 0;  // nothing here may ever take activation or focus
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

bool CompositionSurface::register_class(std::string* error) {
    if (class_registered_) return true;

    HINSTANCE instance = GetModuleHandleW(nullptr);
    // A per-process class name keeps a second Azy instance (or a stale one) from
    // colliding with ours.
    class_name_ = win::to_wide(str_format("AzySkin.Surface.%lu", GetCurrentProcessId()));
    const std::wstring& wide_name = class_name_;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = 0;  // no CS_HREDRAW/CS_VREDRAW: the surface repaints on demand only
    wc.lpfnWndProc = &CompositionSurface::window_proc;
    wc.hInstance = instance;
    wc.hCursor = nullptr;  // never sets a cursor: it can never be hovered
    wc.hbrBackground = nullptr;
    wc.lpszClassName = wide_name.c_str();
    if (RegisterClassExW(&wc) == 0) {
        const DWORD err = GetLastError();
        if (err == ERROR_CLASS_ALREADY_EXISTS) {
            class_registered_ = true;
            return true;
        }
        if (error) *error = "RegisterClassEx failed: " + to_utf8(last_error_text(err));
        return false;
    }
    class_registered_ = true;
    return true;
}

bool CompositionSurface::ensure_created(std::string* error) {
    if (hwnd_ != nullptr) return true;
    if (!register_class(error)) return false;

    const std::wstring& wide_name = class_name_;
    hwnd_ = CreateWindowExW(
        static_cast<DWORD>(input_guard::kRequiredExStyles), wide_name.c_str(), L"Azy Skin",
        WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (hwnd_ == nullptr) {
        if (error) *error = "CreateWindowEx(surface) failed: " + to_utf8(last_error_text());
        return false;
    }

    std::string verify_error;
    if (!input_guard::verify(hwnd_, &verify_error)) {
        if (error) *error = "input-safety contract violated: " + verify_error;
        destroy();
        return false;
    }

    // Defensive: never let this window be owned by, or owned with, anything.
    SetWindowLongPtrW(hwnd_, GWLP_HWNDPARENT, 0);
    return true;
}

bool CompositionSurface::present(HWND below, const Rect& screen_rect, const RingVisual& visual,
                                 std::string* error) {
    if (screen_rect.empty()) {
        if (error) *error = "refusing to present an empty rect";
        return false;
    }
    if (!ensure_created(error)) return false;
    if (!renderer_.render(visual, error)) return false;

    const int width = renderer_.width();
    const int height = renderer_.height();

    // Position, size and z-order in one call. Inserting directly above the
    // Premiere window means the surface never covers another application and
    // disappears with Premiere when it is minimised or covered.
    const HWND insert_after = (below != nullptr && IsWindow(below)) ? below : HWND_TOPMOST;
    if (!SetWindowPos(hwnd_, insert_after, screen_rect.left, screen_rect.top, width, height,
                      SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW)) {
        if (error) *error = "SetWindowPos(surface) failed: " + to_utf8(last_error_text());
        return false;
    }

    HDC screen_dc = GetDC(nullptr);
    if (screen_dc == nullptr) {
        if (error) *error = "GetDC(screen) failed";
        return false;
    }
    POINT destination{screen_rect.left, screen_rect.top};
    POINT source{0, 0};
    SIZE size{width, height};
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;  // premultiplied ARGB, matching the DIB
    const BOOL ok = UpdateLayeredWindow(hwnd_, screen_dc, &destination, &size, renderer_.memory_dc(), &source, 0,
                                        &blend, ULW_ALPHA);
    ReleaseDC(nullptr, screen_dc);
    if (!ok) {
        if (error) *error = "UpdateLayeredWindow failed: " + to_utf8(last_error_text());
        return false;
    }

    last_rect_ = Rect::from_size(screen_rect.left, screen_rect.top, width, height);
    if (!visible_) {
        visible_ = true;
        log_debug("composition surface shown at (%d,%d) %dx%d @ %u DPI", screen_rect.left, screen_rect.top, width,
                  height, visual.width_px);
    }
    ++presents_;
    return true;
}

void CompositionSurface::hide() {
    if (hwnd_ != nullptr && visible_) {
        ShowWindow(hwnd_, SW_HIDE);
        visible_ = false;
    } else if (hwnd_ != nullptr) {
        // Make sure a never-shown window cannot appear later by accident.
        ShowWindow(hwnd_, SW_HIDE);
        visible_ = false;
    }
}

void CompositionSurface::destroy() {
    if (hwnd_ != nullptr && IsWindow(hwnd_)) {
        DestroyWindow(hwnd_);
    }
    hwnd_ = nullptr;
    visible_ = false;
    renderer_.release();
}

}  // namespace win
}  // namespace azy
