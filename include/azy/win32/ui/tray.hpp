// Azy Skin — Win32 layer: system tray presence.
//
// The tray is Azy's primary home: no window is shown at startup, no taskbar
// button, no Alt+Tab entry. The icon is destroyed on exit, so a killed Azy never
// leaves a ghost icon behind (and one is cleaned up anyway if Explorer restarts
// while Azy is running).
#pragma once

#include <functional>
#include <string>

#include "azy/core/theme.hpp"
#include "azy/win32/os/win_compat.hpp"

#include <shellapi.h>  // NOTIFYICONDATAW, Shell_NotifyIconW

namespace azy {
namespace win {

class TrayIcon {
public:
    struct Callbacks {
        std::function<void(bool enabled)> on_toggle_skin;
        std::function<void(ThemeId theme)> on_theme;
        std::function<void()> on_settings;
        std::function<void(bool enabled)> on_start_with_windows;
        std::function<void(bool suspended)> on_suspend;
        std::function<void()> on_reload_settings;
        std::function<void()> on_open_log;
        std::function<void()> on_exit;
    };

    struct ViewState {
        bool skin_enabled = true;
        bool suspended = false;
        bool start_with_windows = false;
        ThemeId theme = ThemeId::AzyDarkGlass;
        std::string status_line;  // e.g. "Premiere Pro 2025 - Azy Dark Glass active"
    };

    ~TrayIcon() { destroy(); }

    bool create(HWND owner, Callbacks callbacks, std::string* error);
    void destroy();
    bool exists() const { return added_; }

    void update(const ViewState& state);

    // Call from the owner window procedure for our registered message.
    // Handles both the modern (NOTIFYICON_VERSION_4) and the legacy layout.
    void handle_message(WPARAM wparam, LPARAM lparam);
    // Call for WM_ENDSESSION/explorer restart.
    void recreate_after_shell_restart();

    void notify(const std::wstring& title, const std::wstring& text, DWORD flags = NIIF_INFO);

    static UINT tray_message_id();
    static UINT taskbar_created_message_id();

private:
    void show_menu(const POINT* anchor);

    HWND owner_ = nullptr;
    Callbacks callbacks_;
    ViewState state_;
    NOTIFYICONDATAW data_{};
    bool added_ = false;
    bool version4_ = false;  // NOTIFYICON_VERSION_4 accepted by the shell
    // A single physical click can reach us more than once (NIN_SELECT plus the
    // legacy mouse message on some shells). The toggle is debounced so a click
    // always means exactly one ON/OFF, while deliberate rapid toggling still
    // works.
    unsigned long long last_toggle_tick_ = 0;
};

}  // namespace win
}  // namespace azy
