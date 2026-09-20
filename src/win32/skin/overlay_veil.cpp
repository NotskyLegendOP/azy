#include "azy/win32/skin/overlay_veil.hpp"

#include <string>
#include <vector>

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
            auto* self = reinterpret_cast<OverlayVeil*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            if (dc != nullptr && self != nullptr) {
                RECT client{};
                GetClientRect(hwnd, &client);
                HBRUSH brush = self->brush_for_color();
                if (brush != nullptr) FillRect(dc, &client, brush);
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

HBRUSH OverlayVeil::brush_for_color() {
    // The alpha lives in the layered window attributes, not in the brush, so the
    // brush only has to follow the colour channels.
    if (brush_ != nullptr && brush_color_.r == color_.r && brush_color_.g == color_.g &&
        brush_color_.b == color_.b) {
        return brush_;
    }
    if (brush_ != nullptr) {
        DeleteObject(brush_);
        brush_ = nullptr;
    }
    brush_ = CreateSolidBrush(RGB(color_.r, color_.g, color_.b));
    brush_color_ = color_;
    return brush_;
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

bool OverlayVeil::probe_visible(std::string* detail) {
    auto fail = [detail](const char* text) {
        if (detail != nullptr) *detail = text;
        return false;
    };
    if (!visible_ || hwnd_ == nullptr || rect_.empty()) {
        return fail("the overlay is not on screen right now (nothing was presented)");
    }

    // A grid across the middle of the covered area: away from the edge treatment,
    // and spread out so that a few points sitting under another window (the
    // settings window the button was clicked in, for instance) cannot decide the
    // answer on their own.
    const int columns = 5;
    const int rows = 4;
    std::vector<POINT> points;
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            POINT point{};
            point.x = rect_.left + rect_.width() * (2 * column + 1) / (2 * columns);
            point.y = rect_.top + rect_.height() * (2 * row + 1) / (2 * rows);
            points.push_back(point);
        }
    }

    std::vector<COLORREF> shown(points.size(), 0);
    std::vector<COLORREF> bare(points.size(), 0);
    if (!screen_pixels(points.data(), points.size(), shown.data())) {
        return fail("the screen could not be read (locked session?)");
    }

    hide();
    DwmFlush();  // let the hidden frame reach the screen before sampling again
    const bool read_ok = screen_pixels(points.data(), points.size(), bare.data());
    if (hwnd_ != nullptr) ShowWindow(hwnd_, SW_SHOWNA);
    visible_ = true;
    DwmFlush();
    if (!read_ok) return fail("the screen could not be read (locked session?)");

    int changed = 0;
    int max_delta = 0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const int delta = pixel_delta(shown[i], bare[i]);
        if (delta > max_delta) max_delta = delta;
        if (delta >= kPixelChangeThreshold) ++changed;
    }
    // Half the grid is enough: the rest may be covered by another window, and the
    // tint is uniform, so the points that did change prove it is there.
    const bool on_screen = changed * 2 >= static_cast<int>(points.size()) && max_delta >= kPixelChangeThreshold;
    if (detail != nullptr) {
        *detail = str_format("%s: %d of %zu sampled screen pixels changed by up to %d/255 when the tint "
                             "was hidden",
                             on_screen ? "on screen" : "NOT on screen", changed, points.size(), max_delta);
    }
    return on_screen;
}

bool OverlayVeil::reposition(HWND below, HWND ring_strip) {
    if (!visible_ || hwnd_ == nullptr) return false;
    const HWND anchor = (ring_strip != nullptr && IsWindow(ring_strip)) ? ring_strip : z_order_anchor(below);
    SetWindowPos(hwnd_, anchor, rect_.left, rect_.top, rect_.width(), rect_.height(),
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    const bool above = below == nullptr || !IsWindow(below) || window_is_above(hwnd_, below);
    if (!above) {
        log_warn("overlay: the veil is no longer above the Premiere window (z-order blocked)");
        hide();
    }
    return above;
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
    if (brush_ != nullptr) {
        DeleteObject(brush_);
        brush_ = nullptr;
    }
    brush_color_ = Rgba{0, 0, 0, 0};
    visible_ = false;
    painted_ = false;
    rect_ = Rect{};
}

}  // namespace win
}  // namespace azy
