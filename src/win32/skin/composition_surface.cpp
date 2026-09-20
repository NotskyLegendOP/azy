#include "azy/win32/skin/composition_surface.hpp"

#include "azy/core/ring_layout.hpp"
#include <cstddef>
#include <string>
#include <vector>

#include "azy/core/log.hpp"
#include "azy/core/strings.hpp"
#include "azy/win32/skin/input_guard.hpp"
#include "azy/win32/os/win_util.hpp"

namespace azy {
namespace win {
namespace {

constexpr int kProbeColumns = 8;  // sample points across the frame edge

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

void CompositionSurface::show_strips() {
    for (Strip& strip : strips_) {
        if (strip.hwnd != nullptr) ShowWindow(strip.hwnd, SW_SHOWNA);
    }
    visible_ = true;
    report_.presented = true;
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

    HINSTANCE instance = GetModuleHandleW(nullptr);
    for (int i = 0; i < kStripCount; ++i) {
        Strip& strip = strips_[i];
        HWND hwnd = CreateWindowExW(static_cast<DWORD>(input_guard::kRequiredExStyles), class_name_.c_str(),
                                    L"Azy Skin Surface", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance,
                                    nullptr);
        if (hwnd == nullptr) {
            if (error) *error = "CreateWindowExW(surface strip) failed: " + to_utf8(last_error_text());
            return false;
        }

        // Verify the input contract on the live window instead of trusting the
        // style bits: a surface that could take a click or the focus is the one
        // failure Azy must never have.
        std::string verify_error;
        if (!input_guard::verify(hwnd, &verify_error)) {
            if (error) *error = "input guard rejected a surface strip: " + verify_error;
            DestroyWindow(hwnd);
            return false;
        }

        strip.hwnd = hwnd;
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

    const RingGeometry geometry = ring_geometry(frame, visual.band_px, visual.radius_px);
    if (!geometry.valid) {
        if (error) *error = "window too small for the four-strip ring";
        return false;
    }
    const RingStrips strips = ring_strip_rects(frame, geometry.thickness_px);
    if (!strips.valid) {
        if (error) *error = "window too small for the four-strip ring";
        return false;
    }

    Rect target;
    switch (index) {
        case kTop:
            target = strips.top;
            break;
        case kBottom:
            target = strips.bottom;
            break;
        case kLeft:
            target = strips.left;
            break;
        case kRight:
        default:
            target = strips.right;
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
    // The clamped radius, so the corner arc is never clipped by the strip that
    // paints it (they are computed together, in ring_geometry).
    local.radius_px = geometry.radius_px;

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

    // The ring must sit directly above Premiere and nowhere else, so the strips
    // follow Premiere's own stacking (they are covered when Premiere is covered,
    // and they vanish with it when it is minimised) instead of floating over
    // unrelated applications.
    const HWND anchor = z_order_anchor(below);

    for (int i = 0; i < kStripCount; ++i) {
        if (!present_strip(i, anchor, frame, visual, error)) {
            // Partial rings look broken: hide everything rather than show three
            // edges out of four.
            report_ = RingReport{};
            report_.frame = frame;
            report_.band_px = visual.band_px;
            report_.radius_px = visual.radius_px;
            report_.error = error != nullptr ? *error : std::string("strip not presented");
            hide();
            return false;
        }
    }

    // The ring is on screen from here on. Log one line per change (present() is
    // only reached when something actually changed, never on a timer), including
    // the two facts that decide whether a user can see it: where it was drawn,
    // and the strongest pixel the renderer produced. A report of "strongest pixel
    // alpha 0" or "not above Premiere" turns "the skin does nothing" from a guess
    // into a diagnosis.
    const RingGeometry reported = ring_geometry(frame, visual.band_px, visual.radius_px);
    unsigned char strongest = 0;
    for (const Strip& strip : strips_) {
        const unsigned char alpha = strip.renderer.max_alpha();
        if (alpha > strongest) strongest = alpha;
    }
    // Confirms the one invariant the whole technique rests on: the strips sit in
    // front of the Premiere window in the z-order. If they ever ended up behind it,
    // every call would still succeed - the ring would simply be composed behind an
    // opaque window and the user would see nothing at all. (A lower-integrity
    // process cannot place its windows above a higher-integrity one, so this is
    // worth checking rather than assuming.)
    const bool above = below == nullptr || !IsWindow(below) || window_is_above(strips_[kTop].hwnd, below);

    // Confirm the strips ended up where they were put. A window that Windows moves
    // back (a virtual desktop switch mid-present, a policy on window placement)
    // produces no error anywhere - just a ring in the wrong place.
    int misplaced = 0;
    for (const Strip& strip : strips_) {
        RECT actual{};
        if (strip.hwnd == nullptr || !GetWindowRect(strip.hwnd, &actual)) continue;
        if (actual.left == strip.rect.left && actual.top == strip.rect.top && actual.right == strip.rect.right &&
            actual.bottom == strip.rect.bottom) {
            continue;
        }
        ++misplaced;
        log_debug("surface strip asked for (%d,%d)-(%d,%d) but sits at (%d,%d)-(%d,%d)", strip.rect.left,
                  strip.rect.top, strip.rect.right, strip.rect.bottom, actual.left, actual.top, actual.right,
                  actual.bottom);
    }

    report_ = RingReport{};
    report_.presented = true;
    report_.frame = frame;
    report_.thickness_px = reported.thickness_px;
    report_.band_px = visual.band_px;
    report_.radius_px = reported.radius_px;
    report_.bitmap_bytes = bitmap_bytes();
    report_.max_alpha = strongest;
    report_.above = above;
    report_.misplaced_strips = misplaced;
    log_info("ring: %dx%d frame at (%d,%d), %dpx thick, band %dpx, radius %dpx, %zu KB, "
             "strongest pixel alpha %u, above Premiere: %s, strips misplaced: %d",
             frame.width(), frame.height(), frame.left, frame.top, reported.thickness_px, visual.band_px,
             reported.radius_px, bitmap_bytes() / 1024, static_cast<unsigned>(strongest),
             above ? "yes" : "no", misplaced);

    // A ring that painted nothing cannot be seen, however well the windows were
    // placed. Every call would have reported success; report the truth instead.
    if (strongest == 0) {
        report_.presented = false;
        report_.error = "the ring bitmap came out empty (nothing would be visible)";
        hide();
        if (error) *error = report_.error;
        log_warn("composition surface: %s", report_.error.c_str());
        return false;
    }

    if (!above) {
        report_.presented = false;
        report_.error = "the strips could not be placed above the Premiere window (z-order blocked)";
        hide();
        if (error) *error = report_.error;
        return false;
    }

    visible_ = true;
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

bool CompositionSurface::probe_visible(std::string* detail) {
    auto fail = [detail](const char* text) {
        if (detail != nullptr) *detail = text;
        return false;
    };
    if (!visible_ || strips_[kTop].hwnd == nullptr || strips_[kTop].rect.empty()) {
        return fail("the ring is not on screen right now (nothing was presented)");
    }

    const Rect strip = strips_[kTop].rect;
    const int max_depth = strip.height() - 1;
    if (max_depth < 1) return fail("the ring strip is too thin to sample");

    std::vector<POINT> points;
    for (int i = 0; i < kProbeColumns; ++i) {
        const int x = strip.left + (strip.width() * (2 * i + 1)) / (2 * kProbeColumns);
        for (int depth = 1; depth <= 4 && depth <= max_depth; ++depth) {
            POINT point{};
            point.x = x;
            point.y = strip.top + depth;
            points.push_back(point);
        }
    }

    std::vector<COLORREF> shown(points.size(), 0);
    std::vector<COLORREF> bare(points.size(), 0);
    if (!screen_pixels(points.data(), points.size(), shown.data())) {
        return fail("the screen could not be read (locked session?)");
    }

    // One frame with the ring hidden. DwmFlush makes the change reach the screen
    // before the second sample, so the two reads are of two different frames.
    hide();
    DwmFlush();
    const bool read_ok = screen_pixels(points.data(), points.size(), bare.data());
    show_strips();
    DwmFlush();
    if (!read_ok) return fail("the screen could not be read (locked session?)");

    int changed = 0;
    int max_delta = 0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const int delta = pixel_delta(shown[i], bare[i]);
        if (delta > max_delta) max_delta = delta;
        if (delta >= kPixelChangeThreshold) ++changed;
    }
    const int needed = static_cast<int>(points.size()) / 2;
    const bool on_screen = changed >= needed && max_delta >= kPixelChangeThreshold;
    if (detail != nullptr) {
        *detail = str_format("%s: %d of %zu sampled screen pixels changed by up to %d/255 when the ring "
                             "was hidden",
                             on_screen ? "on screen" : "NOT on screen", changed, points.size(), max_delta);
    }
    return on_screen;
}

void CompositionSurface::hide() {
    for (Strip& strip : strips_) {
        if (strip.hwnd != nullptr) ShowWindow(strip.hwnd, SW_HIDE);
    }
    visible_ = false;
    report_.presented = false;
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
