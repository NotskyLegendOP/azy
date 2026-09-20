#include "azy/win32/ui/tray.hpp"
#include <string>
#include <utility>

#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM

#include "azy/core/log.hpp"
#include "azy/core/strings.hpp"
#include "azy/win32/os/win_util.hpp"
#include "azy/win32/ui/app_icon.hpp"

namespace azy {
namespace win {
namespace {

constexpr UINT kTrayMessage = WM_APP + 1;

// Menu commands live in their own range: the shell's notification codes are in the
// WM_USER range (NIN_SELECT, NIN_BALLOONUSERCLICK, ...), and a menu id that collided
// with one of them would be indistinguishable from a click on the icon.
enum MenuId : unsigned short {
    kMenuFirst = 2000,
    kStatus = 2000,
    kToggleSkin = 2001,
    kThemeGlass = 2011,
    kThemeDark = 2012,
    kThemeOriginal = 2013,
    kSettings = 2020,
    kStartWithWindows = 2030,
    kSuspend = 2031,
    kReloadSettings = 2040,
    kOpenLog = 2041,
    kRestartElevated = 2042,
    kExit = 2050,
    kMenuLast = 2050,
};

const wchar_t* kTooltipTitle = L"Azy Skin";

}  // namespace

UINT TrayIcon::tray_message_id() { return kTrayMessage; }

UINT TrayIcon::taskbar_created_message_id() {
    // Explorer broadcasts this when the shell (re)starts; the id is registered
    // once and is stable for the lifetime of the session.
    static const UINT id = RegisterWindowMessageW(L"TaskbarCreated");
    return id;
}

bool TrayIcon::create(HWND owner, Callbacks callbacks, std::string* error) {
    if (added_) return true;
    owner_ = owner;
    callbacks_ = std::move(callbacks);

    data_ = NOTIFYICONDATAW{};
    data_.cbSize = sizeof(data_);
    data_.hWnd = owner_;
    data_.uID = 1;
    data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data_.uCallbackMessage = kTrayMessage;
    data_.hIcon = app_icon();
    wcsncpy_s(data_.szTip, kTooltipTitle, _TRUNCATE);

    if (!Shell_NotifyIconW(NIM_ADD, &data_)) {
        if (error) *error = "Shell_NotifyIcon(NIM_ADD) failed: " + to_utf8(last_error_text());
        return false;
    }

    // Windows Vista+ "version 4" icon: proper behaviour with DPI changes, no
    // legacy balloon quirks.
    data_.uVersion = NOTIFYICON_VERSION_4;
    version4_ = Shell_NotifyIconW(NIM_SETVERSION, &data_) != FALSE;
    added_ = true;
    log_info("tray icon created (notification protocol v4: %s)", version4_ ? "yes" : "no");
    return true;
}

void TrayIcon::destroy() {
    if (!added_) return;
    Shell_NotifyIconW(NIM_DELETE, &data_);
    added_ = false;
    log_info("tray icon removed");
}

void TrayIcon::recreate_after_shell_restart() {
    if (!added_) return;
    // Explorer was restarted: the icon is gone from the new shell's notification
    // area, so re-add it. Nothing else about Azy changes.
    added_ = false;
    data_.cbSize = sizeof(data_);
    data_.hWnd = owner_;
    data_.uID = 1;
    data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data_.uCallbackMessage = kTrayMessage;
    data_.hIcon = app_icon();
    wcsncpy_s(data_.szTip, kTooltipTitle, _TRUNCATE);
    if (Shell_NotifyIconW(NIM_ADD, &data_)) {
        data_.uVersion = NOTIFYICON_VERSION_4;
        version4_ = Shell_NotifyIconW(NIM_SETVERSION, &data_) != FALSE;
        added_ = true;
        update(state_);
        log_info("tray icon re-created after Explorer restart");
    }
}

void TrayIcon::update(const ViewState& state) {
    state_ = state;
    if (!added_) return;

    std::wstring tooltip = to_wide(state.status_line);
    if (tooltip.size() > 120) tooltip.resize(120);
    if (tooltip.empty()) tooltip = kTooltipTitle;
    wcsncpy_s(data_.szTip, tooltip.c_str(), _TRUNCATE);

    // Static tooltip text: no NIM_MODIFY burst, no flicker, no polling.
    data_.uFlags = NIF_TIP;
    if (data_.hIcon != nullptr) data_.uFlags |= NIF_ICON;
    Shell_NotifyIconW(NIM_MODIFY, &data_);
}

void TrayIcon::notify(const std::wstring& title, const std::wstring& text, DWORD flags) {
    if (!added_) return;
    data_.uFlags = NIF_INFO;
    wcsncpy_s(data_.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(data_.szInfo, text.c_str(), _TRUNCATE);
    data_.dwInfoFlags = flags;
    Shell_NotifyIconW(NIM_MODIFY, &data_);
}

void TrayIcon::show_menu(const POINT* anchor) {
    POINT cursor{};
    if (anchor != nullptr) {
        cursor = *anchor;
    } else {
        GetCursorPos(&cursor);
    }

    HMENU menu = CreatePopupMenu();
    HMENU theme_menu = CreatePopupMenu();
    if (menu == nullptr || theme_menu == nullptr) {
        if (menu) DestroyMenu(menu);
        if (theme_menu) DestroyMenu(theme_menu);
        return;
    }

    std::wstring status = to_wide(state_.status_line);
    if (status.empty()) status = L"Azy Skin";
    AppendMenuW(menu, MF_STRING | MF_DISABLED, kStatus, status.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kToggleSkin, L"Skin &Enabled");
    CheckMenuItem(menu, kToggleSkin, MF_BYCOMMAND | (state_.skin_enabled ? MF_CHECKED : MF_UNCHECKED));

    AppendMenuW(theme_menu, MF_STRING, kThemeGlass, L"Azy Dark Glass");
    AppendMenuW(theme_menu, MF_STRING, kThemeDark, L"Azy Dark");
    AppendMenuW(theme_menu, MF_STRING, kThemeOriginal, L"Original");
    const unsigned short theme_id = state_.theme == ThemeId::AzyDark      ? kThemeDark
                                    : state_.theme == ThemeId::Original   ? kThemeOriginal
                                                                          : kThemeGlass;
    CheckMenuRadioItem(theme_menu, kThemeGlass, kThemeOriginal, theme_id, MF_BYCOMMAND);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(theme_menu), L"&Theme");

    AppendMenuW(menu, MF_STRING, kSettings, L"&Settings...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kStartWithWindows, L"Start with &Windows");
    CheckMenuItem(menu, kStartWithWindows,
                  MF_BYCOMMAND | (state_.start_with_windows ? MF_CHECKED : MF_UNCHECKED));
    AppendMenuW(menu, MF_STRING, kSuspend, L"&Suspend Skin");
    CheckMenuItem(menu, kSuspend, MF_BYCOMMAND | (state_.suspended ? MF_CHECKED : MF_UNCHECKED));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kReloadSettings, L"&Reload Configuration");
    AppendMenuW(menu, MF_STRING, kOpenLog, L"Open Lo&g File");
    if (state_.elevation_mismatch) {
        // Only offered when it is the actual problem: this is the one situation Azy
        // cannot work around, and the user should not have to guess the fix.
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kRestartElevated,
                    L"Restart as &Administrator (Premiere is elevated)");
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kExit, L"E&xit");

    // A popup menu needs its owner to be the foreground window, otherwise it does
    // not close when the user clicks elsewhere. Azy's window is WS_EX_NOACTIVATE
    // (it can never really take focus), so the previous foreground window is
    // remembered and restored the moment the menu closes: Premiere keeps the
    // focus it had.
    HWND previous_foreground = GetForegroundWindow();
    SetForegroundWindow(owner_);

    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, cursor.x, cursor.y, 0,
                                        owner_, nullptr);
    DestroyMenu(menu);

    if (previous_foreground != nullptr && IsWindow(previous_foreground) &&
        previous_foreground != GetForegroundWindow()) {
        SetForegroundWindow(previous_foreground);
    }

    // Dispatched directly, not through handle_message: a menu command is not a
    // notification, and the notification parser would reject its layout. (Handing
    // menu ids to that parser is exactly how every item in this menu used to end up
    // doing nothing at all.)
    if (command != 0) handle_menu_command(command);
}

void TrayIcon::handle_menu_command(UINT command) {
    switch (command) {
        case kToggleSkin:
            if (callbacks_.on_toggle_skin) callbacks_.on_toggle_skin(!state_.skin_enabled);
            return;
        case kThemeGlass:
            if (callbacks_.on_theme) callbacks_.on_theme(ThemeId::AzyDarkGlass);
            return;
        case kThemeDark:
            if (callbacks_.on_theme) callbacks_.on_theme(ThemeId::AzyDark);
            return;
        case kThemeOriginal:
            if (callbacks_.on_theme) callbacks_.on_theme(ThemeId::Original);
            return;
        case kSettings:
            if (callbacks_.on_settings) callbacks_.on_settings();
            return;
        case kStartWithWindows:
            if (callbacks_.on_start_with_windows) {
                callbacks_.on_start_with_windows(!state_.start_with_windows);
            }
            return;
        case kSuspend:
            if (callbacks_.on_suspend) callbacks_.on_suspend(!state_.suspended);
            return;
        case kReloadSettings:
            if (callbacks_.on_reload_settings) callbacks_.on_reload_settings();
            return;
        case kOpenLog:
            if (callbacks_.on_open_log) callbacks_.on_open_log();
            return;
        case kRestartElevated:
            if (callbacks_.on_restart_elevated) callbacks_.on_restart_elevated();
            return;
        case kExit:
            if (callbacks_.on_exit) callbacks_.on_exit();
            return;
        default:
            return;
    }
}

void TrayIcon::handle_message(WPARAM wparam, LPARAM lparam) {
    // Two notification layouts are possible:
    //   * modern (NOTIFYICON_VERSION_4, which Azy asks for):
    //       LOWORD(wparam) = notification code, HIWORD(wparam) = icon id,
    //       lParam = anchor point (HIWORD/LOWORD = y/x)
    //   * legacy: wparam = icon id, LOWORD(lParam) = notification code
    UINT event = 0;
    POINT anchor{};
    bool have_anchor = false;
    if (version4_) {
        event = LOWORD(wparam);
        const UINT icon_id = HIWORD(wparam);
        anchor.x = GET_X_LPARAM(lparam);
        anchor.y = GET_Y_LPARAM(lparam);
        have_anchor = true;
        if (icon_id != 1) return;
    } else {
        const UINT icon_id = static_cast<UINT>(wparam);
        if (icon_id != 1) return;
        event = LOWORD(lparam);
    }

    auto toggle = [this]() {
        const unsigned long long now = GetTickCount64();
        if (now - last_toggle_tick_ < 120) return;  // duplicate delivery of one click
        last_toggle_tick_ = now;
        if (callbacks_.on_toggle_skin) callbacks_.on_toggle_skin(!state_.skin_enabled);
    };

    switch (event) {
        // In protocol v4 a left click arrives as NIN_SELECT; the legacy messages
        // are not sent (and are ignored here when they are, so nothing toggles
        // twice).
        case NIN_SELECT:
        case WM_LBUTTONUP:
            toggle();
            return;
        case WM_LBUTTONDBLCLK:
        case NIN_KEYSELECT:
            // Double click (or a keyboard selection): open Settings. The tray menu
            // remains the primary route.
            if (event == WM_LBUTTONDBLCLK && callbacks_.on_settings) {
                callbacks_.on_settings();
            } else {
                show_menu(have_anchor ? &anchor : nullptr);
            }
            return;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            show_menu(have_anchor ? &anchor : nullptr);
            return;
        case WM_MBUTTONUP:
            if (callbacks_.on_reload_settings) callbacks_.on_reload_settings();
            return;
        case NIN_BALLOONUSERCLICK:
            if (callbacks_.on_settings) callbacks_.on_settings();
            return;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MOUSEMOVE:
        case NIN_BALLOONSHOW:
        case NIN_BALLOONHIDE:
        case NIN_BALLOONTIMEOUT:
            return;  // deliberately ignored: no animation, no churn
        default:
            break;
    }
}

}  // namespace win
}  // namespace azy
