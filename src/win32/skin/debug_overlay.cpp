#include "azy/win32/skin/debug_overlay.hpp"

#include <algorithm>
#include <string>

#include "azy/core/geometry.hpp"

#include "azy/core/log.hpp"
#include "azy/core/strings.hpp"
#include "azy/win32/os/win_util.hpp"
#include "azy/win32/skin/input_guard.hpp"

namespace azy {
namespace win {
namespace {

// One class for the process: there is exactly one debug overlay per Azy instance.
bool g_class_registered = false;
std::wstring g_class_name;

const COLORREF kBackdrop = RGB(8, 8, 10);
const COLORREF kPanelLine = RGB(96, 176, 255);
const COLORREF kPanelLineHot = RGB(178, 132, 255);
const COLORREF kTextPrimary = RGB(232, 234, 240);
const COLORREF kTextDim = RGB(150, 156, 170);

}  // namespace

LRESULT CALLBACK DebugOverlay::window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        // Identical discipline to the ring and the veil: whatever reaches this
        // window must fall through to Premiere. A debug tool that steals a click
        // would be worse than no debug tool.
        case WM_NCHITTEST:
            input_guard::note_hit_test();
            return HTTRANSPARENT;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_NCACTIVATE:
        case WM_SETFOCUS:
        case WM_ACTIVATE:
            return 0;
        case WM_ERASEBKGND:
            return 1;  // the overlay is painted in one pass
        case WM_PAINT: {
            const auto* self = reinterpret_cast<DebugOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            if (dc != nullptr && self != nullptr) {
                RECT client{};
                GetClientRect(hwnd, &client);

                // Faint backdrop, so the panel outlines are readable over a dark
                // Premiere without hiding what is underneath: 24/255 alpha.
                HBRUSH backdrop = CreateSolidBrush(kBackdrop);
                if (backdrop != nullptr) {
                    // Alpha is applied by the layered window attributes, not here.
                    FillRect(dc, &client, backdrop);
                    DeleteObject(backdrop);
                }

                const int origin_x = self->origin_.left;
                const int origin_y = self->origin_.top;

                const int old_mode = SetBkMode(dc, TRANSPARENT);
                HPEN line = CreatePen(PS_SOLID, 1, kPanelLine);
                HPEN bold = CreatePen(PS_SOLID, 1, kPanelLineHot);
                HGDIOBJ old_pen = SelectObject(dc, line);
                HGDIOBJ old_font =
                    SelectObject(dc, self->font_ != nullptr ? self->font_ : GetStockObject(DEFAULT_GUI_FONT));
                UNREFERENCED_PARAMETER(old_font);

                int index = 0;
                for (const PanelRect& panel : self->panels_) {
                    if (!panel.usable) {
                        ++index;
                        continue;
                    }
                    // Label + outline. Alternating colours make two adjacent
                    // rectangles distinguishable in a screenshot.
                    SelectObject(dc, (index % 2 == 0) ? line : bold);
                    const int left = panel.rect.left - origin_x;
                    const int top = panel.rect.top - origin_y;
                    Rectangle(dc, left, top, panel.rect.right - origin_x, panel.rect.bottom - origin_y);
                    const std::wstring caption =
                        to_wide(str_format("%s  %dx%d", panel_name(panel.id), panel.rect.width(),
                                           panel.rect.height()));
                    SetTextColor(dc, kTextPrimary);
                    TextOutW(dc, left + 6, top + 4, caption.c_str(), static_cast<int>(caption.size()));
                    ++index;
                }

                // The facts block: everything a report needs, in one place, so one
                // screenshot answers every question a maintainer would ask.
                // A facts block that has to stay readable at 200% scaling: lines
                // are spaced in DIP, so they never overlap when the font grows.
                int y = static_cast<int>(dip_to_px(6, static_cast<int>(self->font_dpi_)));
                const int line_height = static_cast<int>(dip_to_px(16, static_cast<int>(self->font_dpi_)));
                const int text_x = static_cast<int>(dip_to_px(10, static_cast<int>(self->font_dpi_)));
                SetTextColor(dc, kTextDim);
                for (const std::string& fact : self->facts_) {
                    const std::wstring line_text = to_wide(fact);
                    TextOutW(dc, text_x, y, line_text.c_str(), static_cast<int>(line_text.size()));
                    y += line_height;
                }

                if (old_font != nullptr) SelectObject(dc, old_font);
                if (old_pen != nullptr) SelectObject(dc, old_pen);
                DeleteObject(line);
                DeleteObject(bold);
                SetBkMode(dc, old_mode);
            }
            EndPaint(hwnd, &paint);
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

bool DebugOverlay::ensure_created(std::string* error) {
    if (hwnd_ != nullptr) return true;

    HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!g_class_registered) {
        g_class_name = to_wide(str_format("AzySkin.Debug.%lu", GetCurrentProcessId()));

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = 0;  // painted on demand, never by the system
        wc.lpfnWndProc = &DebugOverlay::window_proc;
        wc.hInstance = instance;
        wc.hCursor = nullptr;
        wc.hbrBackground = nullptr;
        wc.lpszClassName = g_class_name.c_str();
        if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            if (error) *error = "RegisterClassEx(debug overlay) failed: " + to_utf8(last_error_text());
            return false;
        }
        g_class_registered = true;
    }
    class_name_ = g_class_name;

    HWND hwnd = CreateWindowExW(static_cast<DWORD>(input_guard::kRequiredExStyles), class_name_.c_str(),
                                L"Azy Skin Debug", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
    if (hwnd == nullptr) {
        if (error) *error = "CreateWindowExW(debug overlay) failed: " + to_utf8(last_error_text());
        return false;
    }

    std::string verify_error;
    if (!input_guard::verify(hwnd, &verify_error)) {
        if (error) *error = "input guard rejected the debug overlay: " + verify_error;
        DestroyWindow(hwnd);
        return false;
    }

    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    hwnd_ = hwnd;
    return true;
}

bool DebugOverlay::same_content(const std::vector<PanelRect>& panels,
                                const std::vector<std::string>& facts) const {
    if (panels.size() != panels_.size() || facts.size() != facts_.size()) return false;
    for (size_t i = 0; i < panels.size(); ++i) {
        if (panels[i].id != panels_[i].id || panels[i].usable != panels_[i].usable ||
            panels[i].rect != panels_[i].rect) {
            return false;
        }
    }
    return facts == facts_;
}

void DebugOverlay::ensure_font(unsigned dpi) {
    if (font_ != nullptr && font_dpi_ == static_cast<int>(dpi)) return;
    release_font();
    // Segoe UI at 12 DIP, ClearType: the same font the rest of Azy's UI uses, and
    // the reason the labels stay sharp instead of being a bitmap-scaled stock font.
    const int height = -MulDiv(12, static_cast<int>(dpi == 0 ? 96 : dpi), 96);
    font_ = CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    font_dpi_ = static_cast<int>(dpi == 0 ? 96 : dpi);
}

void DebugOverlay::release_font() {
    if (font_ != nullptr) {
        DeleteObject(font_);
        font_ = nullptr;
    }
    font_dpi_ = 0;
}

bool DebugOverlay::present(HWND below, HWND ring_strip, const Rect& frame, unsigned dpi,
                           const std::vector<PanelRect>& panels, const std::vector<std::string>& facts,
                           std::string* error) {
    if (frame.empty()) {
        if (error) *error = "refusing to draw the debug overlay on an empty frame";
        return false;
    }
    if (!ensure_created(error)) return false;
    ensure_font(dpi);

    if (!same_content(panels, facts)) {
        panels_ = panels;
        facts_ = facts;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    // Same stacking as the veil: directly below the ring (so the ring stays the
    // topmost thing Azy owns), otherwise directly above Premiere.
    const HWND insert_after = (ring_strip != nullptr && IsWindow(ring_strip)) ? ring_strip : z_order_anchor(below);
    if (!SetWindowPos(hwnd_, insert_after, frame.left, frame.top, frame.width(), frame.height(),
                      SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW)) {
        if (error) *error = "SetWindowPos(debug overlay) failed: " + to_utf8(last_error_text());
        return false;
    }

    const bool moved = frame != rect_;
    rect_ = frame;
    origin_ = Rect{frame.left, frame.top, frame.left, frame.top};
    SetLayeredWindowAttributes(hwnd_, 0, 224, LWA_ALPHA);  // readable, still see-through

    // The class paints nothing by itself: after a resize the newly exposed area
    // would stay blank, so repaint synchronously once (never per frame).
    if (moved) {
        InvalidateRect(hwnd_, nullptr, FALSE);
        RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
    }

    if (below != nullptr && IsWindow(below) && !window_is_above(hwnd_, below)) {
        hide();
        if (error) *error = "the debug overlay could not be placed above the Premiere window (z-order blocked)";
        return false;
    }

    visible_ = true;
    return true;
}

void DebugOverlay::hide() {
    if (hwnd_ != nullptr) ShowWindow(hwnd_, SW_HIDE);
    visible_ = false;
}

void DebugOverlay::destroy() {
    if (hwnd_ != nullptr && IsWindow(hwnd_)) DestroyWindow(hwnd_);
    release_font();
    hwnd_ = nullptr;
    panels_.clear();
    facts_.clear();
    visible_ = false;
}

}  // namespace win
}  // namespace azy
