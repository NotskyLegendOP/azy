#include "azy/win32/skin/overlay_veil.hpp"

#include <string>

#include "azy/core/log.hpp"
#include "azy/core/strings.hpp"
#include "azy/win32/os/win_util.hpp"
#include "azy/win32/skin/input_guard.hpp"

namespace azy {
namespace win {
namespace {

// One class for the process, shared by every veil (there is exactly one).
bool g_class_registered = false;
std::wstring g_class_name;

}  // namespace

LRESULT CALLBACK OverlayVeil::window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        // Should never be reached (WS_EX_TRANSPARENT answers hit tests first), but
        // if it is, answer HTTRANSPARENT so the click reaches Premiere.
        case WM_NCHITTEST:
            if (input_guard::hit_test_count() == 0) {
                log_debug("overlay veil was hit-tested (WS_EX_TRANSPARENT did not short-circuit); answering "
                          "HTTRANSPARENT so Premiere keeps the click");
            }
            input_guard::note_hit_test();
            return HTTRANSPARENT;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_NCACTIVATE:
        case WM_SETFOCUS:
        case WM_ACTIVATE:
            return 0;  // nothing here may ever take activation or focus
        case WM_ERASEBKGND:
            return 1;  // the veil is one fill, with no erase pass
        case WM_PAINT: {
            const auto* self = reinterpret_cast<const OverlayVeil*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            if (dc != nullptr && self != nullptr) {
                RECT client{};
                GetClientRect(hwnd, &client);
                HBRUSH brush = CreateSolidBrush(RGB(self->color_.r, self->color_.g, self->color_.b));
                if (brush != nullptr) {
                    FillRect(dc, &client, brush);
                    DeleteObject(brush);
                }
            }
            EndPaint(hwnd, &paint);
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

bool OverlayVeil::ensure_created(std::string* error) {
    if (hwnd_ != nullptr) return true;

    HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!g_class_registered) {
        g_class_name = win::to_wide(str_format("AzySkin.Veil.%lu", GetCurrentProcessId()));

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = 0;  // never repainted by the system: one fill, on demand
        wc.lpfnWndProc = &OverlayVeil::window_proc;
        wc.hInstance = instance;
        wc.hCursor = nullptr;  // can never set a cursor: it cannot be hovered
        wc.hbrBackground = nullptr;
        wc.lpszClassName = g_class_name.c_str();
        if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            if (error) *error = "RegisterClassEx(overlay veil) failed: " + to_utf8(last_error_text());
            return false;
        }
        g_class_registered = true;
    }
    class_name_ = g_class_name;

    HWND hwnd = CreateWindowExW(static_cast<DWORD>(input_guard::kRequiredExStyles), class_name_.c_str(),
                                L"Azy Skin Overlay", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
    if (hwnd == nullptr) {
        if (error) *error = "CreateWindowExW(overlay veil) failed: " + to_utf8(last_error_text());
        return false;
    }

    // The veil is clicked through exactly like the ring strips are: the same styles
    // are required, and the same contract is verified on the live window.
    std::string verify_error;
    if (!input_guard::verify(hwnd, &verify_error)) {
        if (error) *error = "input guard rejected the overlay veil: " + verify_error;
        DestroyWindow(hwnd);
        return false;
    }

    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    hwnd_ = hwnd;
    return true;
}

void OverlayVeil::apply_attributes() {
    if (hwnd_ == nullptr) return;
    // A constant alpha over the whole window: Windows blends one colour, so there
    // is no bitmap and no per-pixel work anywhere.
    SetLayeredWindowAttributes(hwnd_, 0, color_.a, LWA_ALPHA);
}

void OverlayVeil::paint_now() {
    if (hwnd_ == nullptr) return;
    // The class has no CS_HREDRAW/CS_VREDRAW and no background brush, so growing the
    // window does *not* repaint the newly exposed area: without this, the veil would
    // be one pixel of colour in a window the size of Premiere. Repaint explicitly,
    // synchronously, once per change (never per frame).
    InvalidateRect(hwnd_, nullptr, FALSE);
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
    painted_ = true;
}

bool OverlayVeil::present(HWND below, HWND ring_strip, const Rect& frame, const Rgba& color, std::string* error) {
    if (frame.empty()) {
        if (error) *error = "refusing to cover an empty frame";
        return false;
    }
    if (color.a == 0) {
        // No overlay configured: nothing to show (not an error).
        hide();
        return false;
    }
    if (!ensure_created(error)) return false;

    const bool color_changed = !painted_ || color_.r != color.r || color_.g != color.g || color_.b != color.b ||
                               color_.a != color.a;
    const bool rect_changed = frame != rect_;
    color_ = color;
    rect_ = frame;
    if (color_changed) apply_attributes();

    // Directly below the ring when there is one (so the ring's hairline and bezel
    // stay crisp above the tint), otherwise directly above Premiere.
    HWND insert_after = (ring_strip != nullptr && IsWindow(ring_strip)) ? ring_strip : z_order_anchor(below);
    if (!SetWindowPos(hwnd_, insert_after, frame.left, frame.top, frame.width(), frame.height(),
                      SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW)) {
        if (error) *error = "SetWindowPos(overlay veil) failed: " + to_utf8(last_error_text());
        return false;
    }

    // The veil is only useful if it really is above the window it covers: Windows
    // silently refuses that placement when the other process has a higher integrity
    // level (UI privilege isolation), and every call still returns success.
    if (below != nullptr && IsWindow(below) && !window_is_above(hwnd_, below)) {
        hide();
        if (error) {
            *error = "the overlay could not be placed above the Premiere window (z-order blocked)";
        }
        log_warn("overlay: the veil could not be placed above the Premiere window - it is not visible");
        return false;
    }

    // The window has just been resized (and shown), so the client area is painted
    // now - after the new size is known, never before.
    if (color_changed || rect_changed) paint_now();

    visible_ = true;
    if (color_changed || rect_changed) {
        log_info("overlay: %dx%d veil at (%d,%d), tint %u/255 (%d%%) over the whole window", frame.width(),
                 frame.height(), frame.left, frame.top, static_cast<unsigned>(color_.a),
                 static_cast<int>(color_.a * 100 / 255));
    }
    return true;
}

void OverlayVeil::hide() {
    if (hwnd_ != nullptr) ShowWindow(hwnd_, SW_HIDE);
    visible_ = false;
}

void OverlayVeil::destroy() {
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    visible_ = false;
    painted_ = false;
    rect_ = Rect{};
}

}  // namespace win
}  // namespace azy
