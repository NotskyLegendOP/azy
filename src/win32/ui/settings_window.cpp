#include "azy/win32/ui/settings_window.hpp"
#include <string>
#include <utility>

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
    kDiagnosticsText,
    kSectionSkin,
    kSectionAppearance,
    kSectionPerformance,
    kSectionAdvanced,
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
    kPresetCombo,
    kAccentCombo,
    kGlow,
    kGlowLabel,
    kAnimationsCheck,
    kOverlayCheck,
    kOverlay,
    kOverlayLabel,
    kPerformanceMode,
    kSuspendMinimized,
    kSuspendInactive,
    kExperimental,
    kDebugMode,
    kUiProfileCombo,
    kSafeModeText,
    kReenableButton,
    kResetButton,
    kOpenLogButton,
    kCheckButton,
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
    const int controls[] = {kEnableSkin,     kStartWithWindows,  kApplyAutomatically, kThemeCombo,
                            kGlass,          kBorder,            kRadius,         kShadow,
                            kDarkness,       kOverlayCheck,      kOverlay,         kPerformanceMode,
                            kSuspendMinimized, kSuspendInactive,
                            kExperimental,   kReenableButton,    kResetButton,    kOpenLogButton,
                            kCheckButton,    kCloseButton};
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
    const auto px = [dpi](int dip) { return dip_to_px(dip, dpi); };
    // Decorative labels get child id 0 rather than -1: some shell paths treat
    // (UINT)-1 as a special value.
    const auto id_of = [](int id) {
        return id > 0 ? reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)) : static_cast<HMENU>(nullptr);
    };

    // Every control is created explicitly with the class it needs: STATIC for
    // text, BUTTON for check boxes and push buttons, TRACKBAR/COMBOBOX for the
    // inputs. (A check box created as a STATIC would look like text and never send
    // a BN_CLICKED - the kind of bug that only shows up on a real desktop.)
    const auto label = [&](int id, int x_dip, int y_dip, int w_dip, int h_dip, DWORD extra_style,
                           const wchar_t* text, HFONT font) -> HWND {
        HWND control = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT | extra_style,
                                       px(x_dip), px(y_dip), px(w_dip), px(h_dip), hwnd_, id_of(id), nullptr,
                                       nullptr);
        if (control != nullptr && font != nullptr) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
        return control;
    };
    // `enabled` is for the one row that exists but cannot do anything yet: a
    // control that looks clickable and silently does nothing is worse than one
    // that says so, so it is created disabled and reads as unavailable.
    const auto checkbox = [&](int id, int y_dip, int w_dip, const wchar_t* text,
                              bool enabled = true) -> HWND {
        HWND control = CreateWindowExW(0, L"BUTTON", text,
                                       WS_CHILD | WS_VISIBLE | (enabled ? WS_TABSTOP : WS_DISABLED) | BS_AUTOCHECKBOX,
                                       px(kMargin), px(y_dip), px(w_dip), px(kRowHeight), hwnd_, id_of(id), nullptr,
                                       nullptr);
        if (control != nullptr && font_ != nullptr) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }
        return control;
    };
    const auto push_button = [&](int id, int x_dip, int y_dip, int w_dip, const wchar_t* text) -> HWND {
        HWND control = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                       px(x_dip), px(y_dip), px(w_dip), px(26), hwnd_, id_of(id), nullptr,
                                       nullptr);
        if (control != nullptr && font_ != nullptr) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }
        return control;
    };

    const int width = kWindowWidth - kMargin * 2;
    int y = kMargin;

    // --- status ------------------------------------------------------------
    label(kStatusText, kMargin, y, width, kRowHeight, 0, L"Azy Skin", font_bold_);
    y += kRowHeight;
    label(kPremiereText, kMargin, y, width, kRowHeight - 4, SS_ENDELLIPSIS, L"Premiere Pro: not running",
          font_small_);
    y += kRowHeight - 4;
    label(kTreatmentText, kMargin, y, width, kRowHeight - 4, SS_ENDELLIPSIS, L"", font_small_);
    y += kRowHeight - 4;
    // Diagnostics: the two or three facts that decide whether the ring can be
    // seen at all. Small, read-only, and always visible - it turns a "nothing
    // happens" report into a screenshot with the answer in it.
    label(kDiagnosticsText, kMargin, y, width, kRowHeight * 3, SS_LEFT, L"", font_small_);
    y += kRowHeight * 3 + kSectionGap;

    // --- Skin --------------------------------------------------------------
    label(kSectionSkin, kMargin, y, width, 16, 0, L"SKIN", font_small_);
    y += 18;
    checkbox(kEnableSkin, y, width, L"&Enable skin");
    y += kRowHeight;
    checkbox(kStartWithWindows, y, width, L"Start with &Windows");
    y += kRowHeight;
    checkbox(kApplyAutomatically, y, width, L"Apply automatically to Premiere Pro");
    y += kRowHeight + kSectionGap;

    // --- Appearance --------------------------------------------------------
    label(kSectionAppearance, kMargin, y, width, 16, 0, L"APPEARANCE", font_small_);
    y += 18;
    // Quality preset first: it is the one control that changes several sliders at
    // once, so it belongs above them.
    label(-1, kMargin, y, kLabelWidth, kRowHeight, SS_CENTERIMAGE, L"&Preset", font_);
    {
        HWND combo = CreateWindowExW(0, L"COMBOBOX", L"",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                                     px(kMargin + kLabelWidth), px(y), px(200), px(200), hwnd_,
                                     id_of(kPresetCombo), nullptr, nullptr);
        if (combo != nullptr) {
            if (font_ != nullptr) SendMessageW(combo, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Ultra"));
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Balanced (recommended)"));
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Performance"));
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Low power"));
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Custom"));
        }
    }
    y += kRowHeight + 4;
    label(-1, kMargin, y, kLabelWidth, kRowHeight, SS_CENTERIMAGE, L"&Theme", font_);
    {
        HWND combo = CreateWindowExW(0, L"COMBOBOX", L"",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                                     px(kMargin + kLabelWidth), px(y), px(200), px(200), hwnd_,
                                     id_of(kThemeCombo), nullptr, nullptr);
        if (combo != nullptr) {
            if (font_ != nullptr) SendMessageW(combo, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Azy Dark Glass"));
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Azy Dark"));
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Original"));
        }
    }
    y += kRowHeight + 4;
    label(-1, kMargin, y, kLabelWidth, kRowHeight, SS_CENTERIMAGE, L"&Accent", font_);
    {
        HWND combo = CreateWindowExW(0, L"COMBOBOX", L"",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                                     px(kMargin + kLabelWidth), px(y), px(200), px(200), hwnd_,
                                     id_of(kAccentCombo), nullptr, nullptr);
        if (combo != nullptr) {
            if (font_ != nullptr) SendMessageW(combo, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Blue-violet"));
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Blue"));
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Violet"));
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Neutral"));
        }
    }
    y += kRowHeight + 4;

    // The whole-window overlay: the one setting that changes the look of the entire
    // application rather than its edge, so it gets its own row and its own strength.
    checkbox(kOverlayCheck, y, width, L"Cover the whole window (overlay)");
    y += kRowHeight;

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
        {kOverlay, kOverlayLabel, L"&Overlay strength", 100},
        {kGlow, kGlowLabel, L"Accent &glow", 100},
    };
    const int slider_width = 150;
    for (const SliderRow& row : rows) {
        label(-1, kMargin, y, kLabelWidth, kRowHeight, SS_CENTERIMAGE, row.text, font_);
        HWND slider = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS,
                                      px(kMargin + kLabelWidth), px(y), px(slider_width), px(kRowHeight), hwnd_,
                                      id_of(row.slider_id), nullptr, nullptr);
        if (slider != nullptr) {
            SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELPARAM(0, row.maximum));
            SendMessageW(slider, TBM_SETPAGESIZE, 0, 5);
            SendMessageW(slider, TBM_SETLINESIZE, 0, 1);
            if (font_ != nullptr) SendMessageW(slider, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }
        label(row.label_id, kMargin + kLabelWidth + slider_width + 8, y, kValueWidth, kRowHeight, SS_CENTERIMAGE,
              L"0%", font_);
        y += kRowHeight + 2;
    }
    y += kSectionGap;

    // Spec §28 in one checkbox - and the one row that is honest about being
    // inactive. Azy's layers are static by design (the brief asked for no
    // animation and nothing outside Premiere could animate its widgets anyway),
    // so there is no code path behind this switch. The key is still stored and
    // round-tripped so an existing configuration is never rewritten, and the
    // control is disabled rather than hidden so the setting can be found, not
    // guessed at. See docs/KNOWN_LIMITATIONS.md.
    checkbox(kAnimationsCheck, y, width, L"Fade Azy's own layers (not available - Azy is static)", false);
    y += kRowHeight + kSectionGap;

    // --- Performance -------------------------------------------------------
    label(kSectionPerformance, kMargin, y, width, 16, 0, L"PERFORMANCE", font_small_);
    y += 18;
    checkbox(kPerformanceMode, y, width, L"&Performance mode (static colors, minimal monitoring)");
    y += kRowHeight;
    checkbox(kSuspendMinimized, y, width, L"Suspend while Premiere is minimized");
    y += kRowHeight;
    checkbox(kSuspendInactive, y, width, L"Suspend while Premiere is inactive");
    y += kRowHeight + kSectionGap;

    // --- Advanced ----------------------------------------------------------
    label(kSectionAdvanced, kMargin, y, width, 16, 0, L"ADVANCED", font_small_);
    y += 18;
    checkbox(kExperimental, y, width, L"Enable &experimental visual features");
    y += kRowHeight;
    // Debug mode (spec 41): draws the panel map Azy currently believes in over
    // the tracked window, plus the facts around it. It exists so one screenshot
    // from a user is enough to correct the model, and it costs nothing while off.
    checkbox(kDebugMode, y, width, L"&Debug mode: show the panel map over the window");
    y += kRowHeight;
    label(-1, kMargin, y, kLabelWidth, kRowHeight, SS_CENTERIMAGE, L"UI &profile", font_);
    {
        HWND combo = CreateWindowExW(0, L"COMBOBOX", L"",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                                     px(kMargin + kLabelWidth), px(y), px(200), px(200), hwnd_,
                                     id_of(kUiProfileCombo), nullptr, nullptr);
        if (combo != nullptr) {
            if (font_ != nullptr) SendMessageW(combo, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Auto (Editing layout)"));
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Editing"));
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Color"));
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Audio"));
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Effects"));
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Graphics"));
        }
    }
    y += kRowHeight + kSectionGap;
    label(kSafeModeText, kMargin, y, width, kRowHeight * 2, SS_WORDELLIPSIS, L"", font_small_);
    y += kRowHeight * 2;

    push_button(kReenableButton, kMargin, y, 150, L"Re-enable features");
    push_button(kResetButton, kMargin + 158, y, 110, L"Reset config");

    const int bottom_y = y + 34;
    push_button(kOpenLogButton, kMargin, bottom_y, 110, L"Open log file");
    // The one command that answers "is any of this actually on my screen?": it
    // measures the desktop itself rather than trusting the API return values.
    push_button(kCheckButton, kMargin + 118, bottom_y, 128, L"Check visibility");
    push_button(kCloseButton, kWindowWidth - kMargin - 90, bottom_y, 90, L"Close");

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
    check(kOverlayCheck, settings_.appearance.overlay);
    check(kAnimationsCheck, settings_.appearance.animations);
    check(kDebugMode, settings_.debug_mode);

    if (HWND combo = GetDlgItem(hwnd_, kUiProfileCombo)) {
        const int index = settings_.ui_profile == WorkspaceId::Editing    ? 1
                          : settings_.ui_profile == WorkspaceId::Color    ? 2
                          : settings_.ui_profile == WorkspaceId::Audio    ? 3
                          : settings_.ui_profile == WorkspaceId::Effects  ? 4
                          : settings_.ui_profile == WorkspaceId::Graphics ? 5
                                                                          : 0;
        SendMessageW(combo, CB_SETCURSEL, index, 0);
    }

    if (HWND combo = GetDlgItem(hwnd_, kPresetCombo)) {
        // Custom is index 4 and is only ever shown when the values really are the
        // user's own: the combo must never claim a preset that is not in effect.
        const PresetId preset = settings_.current_preset();
        const int index = preset == PresetId::Ultra         ? 0
                          : preset == PresetId::Balanced    ? 1
                          : preset == PresetId::Performance ? 2
                          : preset == PresetId::LowPower    ? 3
                                                            : 4;
        SendMessageW(combo, CB_SETCURSEL, index, 0);
    }
    if (HWND combo = GetDlgItem(hwnd_, kAccentCombo)) {
        const int index = settings_.appearance.accent == AccentId::Blue   ? 1
                          : settings_.appearance.accent == AccentId::Violet ? 2
                          : settings_.appearance.accent == AccentId::Neutral ? 3
                                                                            : 0;
        SendMessageW(combo, CB_SETCURSEL, index, 0);
    }

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
    slider(kOverlay, static_cast<int>(settings_.appearance.overlay_intensity * 100.0 + 0.5));
    slider(kGlow, static_cast<int>(settings_.appearance.glow_intensity * 100.0 + 0.5));

    // The strength only means anything while the overlay is on.
    if (HWND control = GetDlgItem(hwnd_, kOverlay)) {
        EnableWindow(control, settings_.appearance.overlay);
    }

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
    set(kOverlayLabel, format_percent(settings_.appearance.overlay_intensity));
    set(kGlowLabel, format_percent(settings_.appearance.glow_intensity));
}

void SettingsWindow::update_status(const Status& status, bool skin_enabled, bool suspended) {
    status_ = status;
    skin_enabled_ = skin_enabled;
    suspended_ = suspended;
    if (hwnd_ == nullptr) return;

    // "active" here means the ring is on screen, not merely that the settings
    // allow it: the state comes from the skin engine itself.
    std::wstring headline = L"Azy Skin - ";
    if (!skin_enabled) {
        headline += L"off";
    } else if (suspended) {
        headline += L"suspended";
    } else if (!status.state.empty()) {
        headline += to_wide(status.state);
    } else {
        headline += L"starting";
    }
    if (HWND control = GetDlgItem(hwnd_, kStatusText)) SetWindowTextW(control, headline.c_str());

    const std::wstring premiere = L"Premiere Pro: " + to_wide(status.premiere);
    if (HWND control = GetDlgItem(hwnd_, kPremiereText)) SetWindowTextW(control, premiere.c_str());

    const std::wstring treatment = L"Treatment: " + to_wide(status.treatment) + L"   |   " + to_wide(status.host);
    if (HWND control = GetDlgItem(hwnd_, kTreatmentText)) {
        SetWindowTextW(control, treatment.c_str());
        InvalidateRect(control, nullptr, TRUE);
    }

    std::wstring diagnostics;
    for (const std::string& line : status.lines) {
        if (!diagnostics.empty()) diagnostics += L"\r\n";
        diagnostics += to_wide(line);
    }
    if (HWND control = GetDlgItem(hwnd_, kDiagnosticsText)) {
        SetWindowTextW(control, diagnostics.c_str());
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

void SettingsWindow::refresh(const Settings& settings, const Status& status, bool skin_enabled, bool suspended) {
    // Never touch a hidden window (its controls may not exist yet) and never bring
    // it to the foreground: Premiere keeps focus.
    if (hwnd_ == nullptr || !IsWindowVisible(hwnd_)) return;
    settings_ = settings;
    status_ = status;
    skin_enabled_ = skin_enabled;
    suspended_ = suspended;
    sync_controls();
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
                                   control_id == kSafeModeText || control_id == kSectionSkin ||
                                   control_id == kSectionAppearance || control_id == kSectionPerformance ||
                                   control_id == kSectionAdvanced || control_id == kGlassLabel ||
                                   control_id == kBorderLabel || control_id == kRadiusLabel ||
                                   control_id == kShadowLabel || control_id == kDarknessLabel ||
                                   control_id == kOverlayLabel || control_id == kGlowLabel;
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
                case kOverlay: settings_.appearance.overlay_intensity = value / 100.0; break;
                case kGlow: settings_.appearance.glow_intensity = value / 100.0; break;
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
                case kOverlayCheck: {
                    settings_.appearance.overlay =
                        SendMessageW(GetDlgItem(hwnd_, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
                    if (HWND slider = GetDlgItem(hwnd_, kOverlay)) {
                        EnableWindow(slider, settings_.appearance.overlay);
                    }
                    push_settings();
                    return 0;
                }
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
                case kPresetCombo:
                    if (notification == CBN_SELCHANGE) {
                        const int index = static_cast<int>(SendMessageW(GetDlgItem(hwnd_, id), CB_GETCURSEL, 0, 0));
                        // "Custom" is a state, not a preset: selecting it changes
                        // nothing (there is no way back to values that were never
                        // recorded), so it is simply left alone.
                        const PresetId preset = index == 0   ? PresetId::Ultra
                                                : index == 1 ? PresetId::Balanced
                                                : index == 2 ? PresetId::Performance
                                                : index == 3 ? PresetId::LowPower
                                                             : PresetId::Custom;
                        if (preset != PresetId::Custom) {
                            settings_.apply_preset(preset);
                            sync_controls();
                            push_settings();
                        }
                    }
                    return 0;
                case kAccentCombo:
                    if (notification == CBN_SELCHANGE) {
                        const int index = static_cast<int>(SendMessageW(GetDlgItem(hwnd_, id), CB_GETCURSEL, 0, 0));
                        settings_.appearance.accent = index == 1   ? AccentId::Blue
                                                      : index == 2 ? AccentId::Violet
                                                      : index == 3 ? AccentId::Neutral
                                                                   : AccentId::BlueViolet;
                        push_settings();
                    }
                    return 0;
                case kDebugMode:
                    settings_.debug_mode =
                        SendMessageW(GetDlgItem(hwnd_, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
                    push_settings();
                    return 0;
                case kUiProfileCombo:
                    if (notification == CBN_SELCHANGE) {
                        const int index =
                            static_cast<int>(SendMessageW(GetDlgItem(hwnd_, id), CB_GETCURSEL, 0, 0));
                        settings_.ui_profile = index == 1   ? WorkspaceId::Editing
                                               : index == 2 ? WorkspaceId::Color
                                               : index == 3 ? WorkspaceId::Audio
                                               : index == 4 ? WorkspaceId::Effects
                                               : index == 5 ? WorkspaceId::Graphics
                                                            : WorkspaceId::Auto;
                        push_settings();
                    }
                    return 0;
                case kAnimationsCheck:
                    settings_.appearance.animations =
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
                case kCheckButton: {
                    const std::string report =
                        callbacks_.on_check_visibility ? callbacks_.on_check_visibility() : std::string();
                    // A message box rather than a label: the text can be selected
                    // with Ctrl+C and pasted into a report, which is the point of
                    // the command.
                    MessageBoxW(hwnd_, to_wide(report).c_str(), L"Azy Skin - visibility check",
                                MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
                    return 0;
                }
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
