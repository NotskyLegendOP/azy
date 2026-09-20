#include "azy/win32/ui/settings_window.hpp"

#include <commctrl.h>

#include <algorithm>

#include "azy/core/log.hpp"
#include "azy/core/strings.hpp"
#include "azy/win32/os/win_api.hpp"
#include "azy/win32/os/win_util.hpp"
#include "azy/win32/ui/app_icon.hpp"

namespace azy {
namespace win {
namespace {

constexpr wchar_t kWindowClass[] = L"AzySkin.Settings";

enum ControlId : int {
    kStatusText = 100,
    kPremiereText,
    kTreatmentText,
    kEnableSkin,
    kStartWithWindows,
    kApplyAutomatically,
    kThemeCombo,
    kGlass,
    kGlassLabel,
    kBorder,
    kBorderLabel,
    kRadius,
    kRadiusLabel,
    kShadow,
    kShadowLabel,
    kDarkness,
    kDarknessLabel,
    kPerformanceMode,
    kSuspendMinimized,
    kSuspendInactive,
    kExperimental,
    kSafeModeText,
    kReenableButton,
    kResetButton,
    kOpenLogButton,
    kCloseButton,
};

// Panel geometry in DIP; scaled by DPI at layout time.
constexpr int kWindowWidth = 430;
constexpr int kMargin = 14;
constexpr int kRowHeight = 22;
constexpr int kSectionGap = 10;
constexpr int kLabelWidth = 140;
constexpr int kValueWidth = 46;

const COLORREF kBackgroundColor = RGB(24, 24, 27);
const COLORREF kTextColor = RGB(226, 228, 234);
const COLORREF kDimTextColor = RGB(150, 152, 160);  // secondary/status lines

struct Section {
    const wchar_t* title;
    int y;  // filled during layout
};

std::wstring format_percent(double value) {
    return std::to_wstring(static_cast<int>(value * 100.0 + 0.5)) + L"%";
}

}  // namespace

bool SettingsWindow::create(HINSTANCE instance, Callbacks callbacks, std::string* error) {
    if (hwnd_ != nullptr) return true;
    callbacks_ = std::move(callbacks);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &SettingsWindow::window_proc;
    wc.hInstance = instance;
    wc.hIcon = app_icon();
    wc.hIconSm = app_icon();
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kWindowClass;
    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        if (error) *error = "RegisterClassEx(settings) failed: " + to_utf8(last_error_text());
        return false;
    }

    dpi_ = static_cast<int>(win::system_dpi());
    const int width = dip_to_px(kWindowWidth, dpi_);
    const int height = dip_to_px(560, dpi_);

    hwnd_ = CreateWindowExW(0, kWindowClass, L"Azy Skin - Settings",
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT,
                            width, height, nullptr, nullptr, instance, this);
    if (hwnd_ == nullptr) {
        if (error) *error = "CreateWindowEx(settings) failed: " + to_utf8(last_error_text());
        return false;
    }
    return true;
}

void SettingsWindow::destroy() {
    if (hwnd_ != nullptr && IsWindow(hwnd_)) {
        DestroyWindow(hwnd_);
    }
    hwnd_ = nullptr;
    if (font_ != nullptr) {
        DeleteObject(font_);
        font_ = nullptr;
    }
    if (font_bold_ != nullptr) {
        DeleteObject(font_bold_);
        font_bold_ = nullptr;
    }
    if (font_small_ != nullptr) {
        DeleteObject(font_small_);
        font_small_ = nullptr;
    }
    if (background_ != nullptr) {
        DeleteObject(background_);
        background_ = nullptr;
    }
}

void SettingsWindow::set_dark_theme() {
    // Documented-on-newer-Windows theming of our own window's standard controls.
    // If a control does not honour it, it simply keeps its default look; nothing
    // here can affect Premiere.
    SetWindowTheme(hwnd_, L"DarkMode_Explorer", nullptr);
    const int controls[] = {kEnableSkin,        kStartWithWindows, kApplyAutomatically, kThemeCombo,
                            kGlass,             kBorder,           kRadius,             kShadow,
                            kDarkness,          kPerformanceMode,  kSuspendMinimized,   kSuspendInactive,
                            kExperimental,      kReenableButton,   kResetButton,        kOpenLogButton,
                            kCloseButton};
    for (int id : controls) {
        if (HWND control = GetDlgItem(hwnd_, id)) {
            SetWindowTheme(control, L"DarkMode_Explorer", nullptr);
        }
    }
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd_, dwm_attr::kUseImmersiveDarkMode, &dark, sizeof(dark));
    const DWORD caption = dwm_attr::colorref(24, 24, 27);
    const DWORD border = dwm_attr::colorref(56, 57, 63);
    const DWORD text = dwm_attr::colorref(226, 228, 234);
    DwmSetWindowAttribute(hwnd_, dwm_attr::kCaptionColor, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd_, dwm_attr::kBorderColor, &border, sizeof(border));
    DwmSetWindowAttribute(hwnd_, dwm_attr::kTextColor, &text, sizeof(text));
    if (background_ == nullptr) background_ = CreateSolidBrush(kBackgroundColor);
}

// Destroys and re-creates every child control: used for the initial layout and
// again after a DPI change (which also changes the fonts).
void SettingsWindow::rebuild_controls() {
    if (hwnd_ == nullptr) return;
    for (HWND child = GetWindow(hwnd_, GW_CHILD); child != nullptr;) {
        HWND next = GetWindow(child, GW_HWNDNEXT);
        DestroyWindow(child);
        child = next;
    }
    layout(dpi_);
}

void SettingsWindow::layout(int dpi) {
    auto px = [dpi](int dip) { return dip_to_px(dip, dpi); };
    auto place = [&](int id, int x_dip, int y_dip, int w_dip, int h_dip, DWORD style, const wchar_t* text) -> HWND {
        HWND control = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | style, px(x_dip), px(y_dip),
                                       px(w_dip), px(h_dip), hwnd_,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
        if (control != nullptr && font_ != nullptr) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        return control;
    };

    const int width = kWindowWidth - kMargin * 2;
    int y = kMargin;

    // --- header ------------------------------------------------------------
    place(kStatusText, kMargin, y, width, kRowHeight, SS_LEFT, L"Azy Skin");
    if (HWND control = GetDlgItem(hwnd_, kStatusText)) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_bold_), TRUE);
    }
    y += kRowHeight;
    place(kPremiereText, kMargin, y, width, kRowHeight - 4, SS_LEFT | SS_ENDELLIPSIS, L"Premiere Pro: not running");
    if (HWND control = GetDlgItem(hwnd_, kPremiereText)) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_small_), TRUE);
    }
    y += kRowHeight - 4;
    place(kTreatmentText, kMargin, y, width, kRowHeight - 4, SS_LEFT | SS_ENDELLIPSIS, L"");
    if (HWND control = GetDlgItem(hwnd_, kTreatmentText)) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_small_), TRUE);
    }
    y += kRowHeight + kSectionGap;

    // --- Skin --------------------------------------------------------------
    place(-1, kMargin, y, width, 16, SS_LEFT, L"SKIN");
    y += 18;
    place(kEnableSkin, kMargin, y, width, kRowHeight, BS_AUTOCHECKBOX | WS_TABSTOP, L"&Enable skin");
    y += kRowHeight;
    place(kStartWithWindows, kMargin, y, width, kRowHeight, BS_AUTOCHECKBOX | WS_TABSTOP,
          L"Start with &Windows");
    y += kRowHeight;
    place(kApplyAutomatically, kMargin, y, width, kRowHeight, BS_AUTOCHECKBOX | WS_TABSTOP,
          L"Apply automatically to Premiere Pro");
    y += kRowHeight + kSectionGap;

    // --- Appearance --------------------------------------------------------
    place(-1, kMargin, y, width, 16, SS_LEFT, L"APPEARANCE");
    y += 18;
    place(-1, kMargin, y, kLabelWidth, kRowHeight, SS_LEFT, L"&Theme");
    HWND combo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                 px(kMargin + kLabelWidth), px(y), px(200), px(200), hwnd_,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(kThemeCombo)), nullptr, nullptr);
    if (combo) {
        if (font_) SendMessageW(combo, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Azy Dark Glass"));
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Azy Dark"));
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Original"));
    }
    y += kRowHeight + 4;

    struct SliderRow {
        int slider_id;
        int label_id;
        const wchar_t* text;
        int maximum;
    };
    const SliderRow rows[] = {
        {kGlass, kGlassLabel, L"&Glass intensity", 100},
        {kBorder, kBorderLabel, L"&Border intensity", 100},
        {kRadius, kRadiusLabel, L"Corner &radius", 16},
        {kShadow, kShadowLabel, L"&Shadow intensity", 100},
        {kDarkness, kDarknessLabel, L"Overall dar&kness", 100},
    };
    const int slider_width = 150;
    for (const SliderRow& row : rows) {
        place(-1, kMargin, y, kLabelWidth, kRowHeight, SS_LEFT, row.text);
        HWND slider = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS,
                                      px(kMargin + kLabelWidth), px(y),
                                      px(slider_width), px(kRowHeight), hwnd_,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(row.slider_id)), nullptr, nullptr);
        if (slider) {
            SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELPARAM(0, row.maximum));
            SendMessageW(slider, TBM_SETPAGESIZE, 0, 5);
            if (font_) SendMessageW(slider, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }
        place(row.label_id, kMargin + kLabelWidth + slider_width + 8, y, kValueWidth, kRowHeight,
              SS_LEFT | SS_CENTERIMAGE, L"0%");
        y += kRowHeight + 2;
    }
    y += kSectionGap;

    // --- Performance -------------------------------------------------------
    place(-1, kMargin, y, width, 16, SS_LEFT, L"PERFORMANCE");
    y += 18;
    place(kPerformanceMode, kMargin, y, width, kRowHeight, BS_AUTOCHECKBOX | WS_TABSTOP,
          L"&Performance mode (static colors, minimal monitoring)");
    y += kRowHeight;
    place(kSuspendMinimized, kMargin, y, width, kRowHeight, BS_AUTOCHECKBOX | WS_TABSTOP,
          L"Suspend while Premiere is minimized");
    y += kRowHeight;
    place(kSuspendInactive, kMargin, y, width, kRowHeight, BS_AUTOCHECKBOX | WS_TABSTOP,
          L"Suspend while Premiere is inactive");
    y += kRowHeight + kSectionGap;

    // --- Advanced ----------------------------------------------------------
    place(-1, kMargin, y, width, 16, SS_LEFT, L"ADVANCED");
    y += 18;
    place(kExperimental, kMargin, y, width, kRowHeight, BS_AUTOCHECKBOX | WS_TABSTOP,
          L"Enable &experimental visual features");
    y += kRowHeight;
    place(kSafeModeText, kMargin, y, width, kRowHeight * 2, SS_LEFT, L"");
    if (HWND control = GetDlgItem(hwnd_, kSafeModeText)) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_small_), TRUE);
    }
    y += kRowHeight * 2;

    auto button = [&](int id, int x_dip, int y_dip, int w_dip, const wchar_t* text) {
        HWND control = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                       px(x_dip), px(y_dip), px(w_dip), px(26), hwnd_,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
        if (control && font_) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        return control;
    };
    button(kReenableButton, kMargin, y, 150, L"Re-enable features");
    button(kResetButton, kMargin + 158, y, 110, L"Reset config");

    const int bottom_y = y + 34;
    button(kOpenLogButton, kMargin, bottom_y, 110, L"Open log file");
    button(kCloseButton, kWindowWidth - kMargin - 90, bottom_y, 90, L"Close");

    set_dark_theme();
    sync_controls();

    // Size the window to exactly contain the controls.
    RECT outer{0, 0, px(kWindowWidth), px(bottom_y + 40)};
    AdjustWindowRectEx(&outer, static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE)), FALSE,
                       static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_EXSTYLE)));
    SetWindowPos(hwnd_, nullptr, 0, 0, outer.right - outer.left, outer.bottom - outer.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void SettingsWindow::sync_controls() {
    creating_ = true;

    auto check = [&](int id, bool value) {
        if (HWND control = GetDlgItem(hwnd_, id)) {
            SendMessageW(control, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
        }
    };
    auto slider = [&](int id, int value) {
        if (HWND control = GetDlgItem(hwnd_, id)) {
            SendMessageW(control, TBM_SETPOS, TRUE, value);
        }
    };

    check(kEnableSkin, settings_.enabled);
    check(kStartWithWindows, settings_.start_with_windows);
    check(kApplyAutomatically, settings_.apply_automatically);
    check(kPerformanceMode, settings_.performance_mode);
    check(kSuspendMinimized, settings_.suspend_when_minimized);
    check(kSuspendInactive, settings_.suspend_when_inactive);
    check(kExperimental, settings_.experimental);

    if (HWND combo = GetDlgItem(hwnd_, kThemeCombo)) {
        const int index = settings_.appearance.theme == ThemeId::AzyDark      ? 1
                          : settings_.appearance.theme == ThemeId::Original   ? 2
                                                                             : 0;
        SendMessageW(combo, CB_SETCURSEL, index, 0);
    }

    slider(kGlass, static_cast<int>(settings_.appearance.glass_intensity * 100.0 + 0.5));
    slider(kBorder, static_cast<int>(settings_.appearance.border_intensity * 100.0 + 0.5));
    slider(kRadius, settings_.appearance.corner_radius_dip);
    slider(kShadow, static_cast<int>(settings_.appearance.shadow_intensity * 100.0 + 0.5));
    slider(kDarkness, static_cast<int>(settings_.appearance.darkness * 100.0 + 0.5));

    if (HWND control = GetDlgItem(hwnd_, kReenableButton)) {
        EnableWindow(control, settings_.safe_mode || !settings_.experimental);
    }

    update_slider_labels();
    update_status(status_, skin_enabled_, suspended_);
    creating_ = false;
}

void SettingsWindow::update_slider_labels() {
    auto set = [&](int id, const std::wstring& text) {
        if (HWND control = GetDlgItem(hwnd_, id)) SetWindowTextW(control, text.c_str());
    };
    set(kGlassLabel, format_percent(settings_.appearance.glass_intensity));
    set(kBorderLabel, format_percent(settings_.appearance.border_intensity));
    set(kRadiusLabel, std::to_wstring(settings_.appearance.corner_radius_dip) + L" px");
    set(kShadowLabel, format_percent(settings_.appearance.shadow_intensity));
    set(kDarknessLabel, format_percent(settings_.appearance.darkness));
}

void SettingsWindow::update_status(const Status& status, bool skin_enabled, bool suspended) {
    status_ = status;
    skin_enabled_ = skin_enabled;
    suspended_ = suspended;
    if (hwnd_ == nullptr) return;

    std::wstring headline = L"Azy Skin - ";
    if (!skin_enabled) {
        headline += L"off";
    } else if (suspended) {
        headline += L"suspended";
    } else {
        headline += L"active";
    }
    if (HWND control = GetDlgItem(hwnd_, kStatusText)) SetWindowTextW(control, headline.c_str());

    const std::wstring premiere = L"Premiere Pro: " + to_wide(status.premiere);
    if (HWND control = GetDlgItem(hwnd_, kPremiereText)) SetWindowTextW(control, premiere.c_str());

    const std::wstring treatment = L"Treatment: " + to_wide(status.treatment) + L"   |   " + to_wide(status.host);
    if (HWND control = GetDlgItem(hwnd_, kTreatmentText)) {
        SetWindowTextW(control, treatment.c_str());
        InvalidateRect(control, nullptr, TRUE);
    }

    std::wstring note = to_wide(status.safe_mode_note);
    if (note.empty()) note = L"Safe mode is off. Experimental features are opt-in.";
    if (HWND control = GetDlgItem(hwnd_, kSafeModeText)) SetWindowTextW(control, note.c_str());
}

void SettingsWindow::push_settings() {
    if (creating_) return;
    settings_.clamp();
    if (callbacks_.on_change) callbacks_.on_change(settings_);
}

void SettingsWindow::show(const Settings& settings, const Status& status) {
    settings_ = settings;
    status_ = status;
    if (hwnd_ == nullptr) return;
    sync_controls();
    if (dpi_ == 0) dpi_ = static_cast<int>(win::system_dpi());
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
}

void SettingsWindow::hide() {
    if (hwnd_ != nullptr) ShowWindow(hwnd_, SW_HIDE);
}

bool SettingsWindow::visible() const { return hwnd_ != nullptr && IsWindowVisible(hwnd_) != FALSE; }

LRESULT CALLBACK SettingsWindow::window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        auto* self = static_cast<SettingsWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
        return TRUE;
    }
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) return DefWindowProcW(hwnd, message, wparam, lparam);
    return self->handle(hwnd, message, wparam, lparam);
}

LRESULT SettingsWindow::handle(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE: {
            dpi_ = static_cast<int>(win::dpi_for_window(hwnd));
            if (dpi_ == 0) dpi_ = 96;
            const int height = -MulDiv(10, dpi_, 72);  // 10pt UI font
            font_ = CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            font_bold_ = CreateFontW(height, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                     OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                     DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            font_small_ = CreateFontW(-MulDiv(9, dpi_, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                      DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            background_ = CreateSolidBrush(kBackgroundColor);
            rebuild_controls();
            return 0;
        }
        case WM_ERASEBKGND: {
            RECT rect{};
            GetClientRect(hwnd, &rect);
            FillRect(reinterpret_cast<HDC>(wparam), &rect, background_);
            return 1;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            const int control_id = GetDlgCtrlID(reinterpret_cast<HWND>(lparam));
            const bool secondary = control_id == kPremiereText || control_id == kTreatmentText ||
                                   control_id == kSafeModeText;
            SetTextColor(dc, secondary ? kDimTextColor : kTextColor);
            SetBkColor(dc, kBackgroundColor);
            return reinterpret_cast<LRESULT>(background_);
        }
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            SetTextColor(dc, kTextColor);
            SetBkColor(dc, kBackgroundColor);
            return reinterpret_cast<LRESULT>(background_);
        }
        case WM_HSCROLL: {
            HWND control = reinterpret_cast<HWND>(lparam);
            if (control == nullptr) return 0;
            const int id = GetDlgCtrlID(control);
            const int value = static_cast<int>(SendMessageW(control, TBM_GETPOS, 0, 0));
            switch (id) {
                case kGlass: settings_.appearance.glass_intensity = value / 100.0; break;
                case kBorder: settings_.appearance.border_intensity = value / 100.0; break;
                case kRadius: settings_.appearance.corner_radius_dip = value; break;
                case kShadow: settings_.appearance.shadow_intensity = value / 100.0; break;
                case kDarkness: settings_.appearance.darkness = value / 100.0; break;
                default: return 0;
            }
            update_slider_labels();
            push_settings();
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wparam);
            const int notification = HIWORD(wparam);
            switch (id) {
                case kEnableSkin:
                    settings_.enabled =
                        SendMessageW(GetDlgItem(hwnd_, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
                    push_settings();
                    return 0;
                case kStartWithWindows:
                    settings_.start_with_windows =
                        SendMessageW(GetDlgItem(hwnd_, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
                    push_settings();
                    return 0;
                case kApplyAutomatically:
                    settings_.apply_automatically =
                        SendMessageW(GetDlgItem(hwnd_, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
                    push_settings();
                    return 0;
                case kPerformanceMode:
                    settings_.performance_mode =
                        SendMessageW(GetDlgItem(hwnd_, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
                    push_settings();
                    return 0;
                case kSuspendMinimized:
                    settings_.suspend_when_minimized =
                        SendMessageW(GetDlgItem(hwnd_, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
                    push_settings();
                    return 0;
                case kSuspendInactive:
                    settings_.suspend_when_inactive =
                        SendMessageW(GetDlgItem(hwnd_, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
                    push_settings();
                    return 0;
                case kExperimental:
                    settings_.experimental =
                        SendMessageW(GetDlgItem(hwnd_, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
                    if (settings_.experimental) {
                        // Opting in is exactly the action that clears safe mode.
                        settings_.safe_mode = false;
                        settings_.safe_mode_reason.clear();
                    }
                    push_settings();
                    return 0;
                case kThemeCombo:
                    if (notification == CBN_SELCHANGE) {
                        const int index = static_cast<int>(SendMessageW(GetDlgItem(hwnd_, id), CB_GETCURSEL, 0, 0));
                        settings_.appearance.theme = index == 1   ? ThemeId::AzyDark
                                                     : index == 2 ? ThemeId::Original
                                                                  : ThemeId::AzyDarkGlass;
                        push_settings();
                    }
                    return 0;
                case kReenableButton:
                    settings_.safe_mode = false;
                    settings_.safe_mode_reason.clear();
                    settings_.experimental = true;
                    push_settings();
                    return 0;
                case kResetButton:
                    if (callbacks_.on_reset) callbacks_.on_reset();
                    return 0;
                case kOpenLogButton:
                    if (callbacks_.on_open_log) callbacks_.on_open_log();
                    return 0;
                case kCloseButton:
                    hide();
                    return 0;
                default:
                    return 0;
            }
        }
        case WM_CLOSE:
            hide();
            return 0;
        case WM_DESTROY:
            hwnd_ = nullptr;
            return 0;
        case WM_DPICHANGED: {
            // The user dragged the settings window onto a monitor with a
            // different scale factor: rebuild the layout at the new scale so text
            // and controls stay crisp instead of being stretched.
            dpi_ = HIWORD(wparam);
            const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            if (font_) DeleteObject(font_);
            if (font_bold_) DeleteObject(font_bold_);
            if (font_small_) DeleteObject(font_small_);
            const int height = -MulDiv(10, dpi_, 72);
            font_ = CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            font_bold_ = CreateFontW(height, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                     OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                     DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            font_small_ = CreateFontW(-MulDiv(9, dpi_, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            rebuild_controls();
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

}  // namespace win
}  // namespace azy
