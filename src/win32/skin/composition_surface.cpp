#include "azy/win32/skin/composition_surface.hpp"

#include "azy/core/log.hpp"
#include "azy/core/strings.hpp"
#include "azy/win32/skin/input_guard.hpp"
#include "azy/win32/os/win_util.hpp"

namespace azy {
namespace win {
namespace {

// One strip per side. Each strip's thickness must cover the treated band plus the
// corner arc, otherwise a large radius would be visibly cut off at the corners.
int strip_thickness(const RingVisual& visual) {
    const int band = visual.band_px < 1 ? 1 : visual.band_px;
    const int needed = band + 2;  // + the hairline and the bezel rows
    const int corner = visual.radius_px + 1;
    return needed > corner ? needed : corner;
}

}  // namespace

LRESULT CALLBACK CompositionSurface::window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        // Should never be reached (WS_EX_TRANSPARENT answers hit tests before the
        // window procedure is called), but if it ever is, send the point below:
        // Premiere keeps every click.
        case WM_NCHITTEST:
            // Note it once, at debug level: a non-zero count means the surface was
            // reached by a hit test, i.e. the click-through guarantee degraded and
            // the styles need looking at. HTTRANSPARENT still keeps the click below.
            if (input_guard::hit_test_count() == 0) {
                log_debug("composition surface was hit-tested (WS_EX_TRANSPARENT did not short-circuit); "
                          "answering HTTRANSPARENT so Premiere keeps the click");
            }
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

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = 0;  // no CS_HREDRAW/CS_VREDRAW: strips repaint on demand only
    wc.lpfnWndProc = &CompositionSurface::window_proc;
    wc.hInstance = instance;
    wc.hCursor = nullptr;  // never sets a cursor: strips can never be hovered
    wc.hbrBackground = nullptr;
    wc.lpszClassName = class_name_.c_str();
    if (RegisterClassExW(&wc) == 0) {
        const DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            if (error) *error = "RegisterClassEx failed: " + to_utf8(last_error_text(err));
            return false;
        }
    }
    class_registered_ = true;
    return true;
}

bool CompositionSurface::ensure_created(std::string* error) {
    if (strips_[kTop].hwnd != nullptr) return true;
    if (!register_class(error)) return false;

    for (int i = 0; i < kStripCount; ++i) {
        HWND hwnd = CreateWindowExW(static_cast<DWORD>(input_guard::kRequiredExStyles), class_name_.c_str(),
                                    L"Azy Skin", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                                    GetModuleHandleW(nullptr), nullptr);
        if (hwnd == nullptr) {
            if (error) *error = "CreateWindowEx(surface strip) failed: " + to_utf8(last_error_text());
            destroy();
            return false;
        }

        std::string verify_error;
        if (!input_guard::verify(hwnd, &verify_error)) {
            // Never show a surface that could take a click. Fail the whole ring
            // rather than showing a half-safe one.
            if (error) *error = "input-safety contract violated: " + verify_error;
            DestroyWindow(hwnd);
            destroy();
            return false;
        }

        // Defensive: strips are never owned by, or owned with, anything.
        SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT, 0);
        strips_[i].hwnd = hwnd;
    }
    return true;
}

bool CompositionSurface::present_strip(int index, HWND insert_after, const Rect& frame,
                                       const RingVisual& visual, std::string* error) {
    Strip& strip = strips_[index];
    if (strip.hwnd == nullptr) {
        if (error) *error = "surface strip window missing";
        return false;
    }

    const int thickness = strip_thickness(visual);
    Rect target;
    switch (index) {
        case kTop:
            target = Rect::from_size(frame.left, frame.top, frame.width(), thickness);
            break;
        case kBottom:
            target = Rect::from_size(frame.left, frame.bottom - thickness, frame.width(), thickness);
            break;
        case kLeft:
            // The vertical strips stop below the horizontal ones: the corner arcs
            // are part of the horizontal strips, so nothing is painted twice.
            target = Rect::from_size(frame.left, frame.top + thickness, thickness,
                                     frame.height() - 2 * thickness);
            break;
        case kRight:
        default:
            target = Rect::from_size(frame.right - thickness, frame.top + thickness, thickness,
                                     frame.height() - 2 * thickness);
            break;
    }
    if (target.empty()) {
        if (error) *error = "refusing to present an empty strip";
        return false;
    }

    // The renderer needs to know where this strip sits inside the frame so it can
    // draw the shared ring in frame coordinates.
    RingVisual local = visual;
    local.origin_x = target.left - frame.left;
    local.origin_y = target.top - frame.top;

    if (!strip.renderer.prepare(target.width(), target.height(), error)) return false;
    if (!strip.renderer.render(local, error)) return false;

    // SetWindowPos places the window *behind* hWndInsertAfter, so "directly above
    // Premiere" means inserting after whatever currently precedes Premiere (see
    // present()). SWP_NOOWNERZORDER keeps ownership (there is none) untouched.
    if (!SetWindowPos(strip.hwnd, insert_after, target.left, target.top, target.width(), target.height(),
                      SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW)) {
        if (error) *error = "SetWindowPos(surface strip) failed: " + to_utf8(last_error_text());
        return false;
    }

    HDC screen_dc = GetDC(nullptr);
    if (screen_dc == nullptr) {
        if (error) *error = "GetDC(screen) failed";
        return false;
    }
    POINT destination{target.left, target.top};
    POINT source{0, 0};
    SIZE size{target.width(), target.height()};
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;  // premultiplied ARGB, matching the DIB
    const BOOL ok = UpdateLayeredWindow(strip.hwnd, screen_dc, &destination, &size, strip.renderer.memory_dc(),
                                        &source, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, screen_dc);
    if (!ok) {
        if (error) *error = "UpdateLayeredWindow failed: " + to_utf8(last_error_text());
        return false;
    }

    strip.rect = target;
    return true;
}

bool CompositionSurface::present(HWND below, const Rect& frame, const RingVisual& visual, std::string* error) {
    if (frame.empty()) {
        if (error) *error = "refusing to present an empty frame";
        return false;
    }
    if (!ensure_created(error)) return false;

    // Resolve the z-order anchor once: the ring must sit directly above Premiere
    // and nowhere else, so the strips follow Premiere's own stacking (they are
    // covered when Premiere is covered, and they vanish with it when it is
    // minimised) instead of floating over unrelated applications.
    HWND anchor = HWND_TOPMOST;
    if (below != nullptr && IsWindow(below)) {
        if (GetWindowLongPtrW(below, GWL_EXSTYLE) & WS_EX_TOPMOST) {
            anchor = HWND_TOPMOST;
        } else {
            // The window currently in front of Premiere: inserting the strip after
            // it puts the strip between that window and Premiere.
            anchor = GetWindow(below, GW_HWNDPREV);
            if (anchor == nullptr || anchor == HWND_TOPMOST) anchor = HWND_TOP;
        }
    }

    for (int i = 0; i < kStripCount; ++i) {
        if (!present_strip(i, anchor, frame, visual, error)) {
            // Partial rings look broken: hide everything rather than show three
            // edges out of four.
            hide();
            return false;
        }
    }

    if (!visible_) {
        visible_ = true;
        log_debug("composition surface shown: %dx%d at (%d,%d), 4 strips, band %dpx, radius %dpx, %zu KB of bitmaps",
                  frame.width(), frame.height(), frame.left, frame.top, visual.band_px, visual.radius_px,
                  bitmap_bytes() / 1024);
    }
    ++presents_;
    return true;
}

size_t CompositionSurface::bitmap_bytes() const {
    size_t total = 0;
    for (const Strip& strip : strips_) {
        total += static_cast<size_t>(strip.renderer.width()) * static_cast<size_t>(strip.renderer.height()) * 4u;
    }
    return total;
}

void CompositionSurface::hide() {
    for (Strip& strip : strips_) {
        if (strip.hwnd != nullptr) ShowWindow(strip.hwnd, SW_HIDE);
    }
    visible_ = false;
}

void CompositionSurface::destroy() {
    for (Strip& strip : strips_) {
        if (strip.hwnd != nullptr && IsWindow(strip.hwnd)) DestroyWindow(strip.hwnd);
        strip.hwnd = nullptr;
        strip.renderer.release();
        strip.rect = Rect{};
    }
    visible_ = false;
}

}  // namespace win
}  // namespace azy
