// Azy Skin — Win32 layer: the settings window.
//
// A single small window, created once and re-used, with no dialog resources and
// no framework: pure Win32 controls, dark-themed, live-applied. Every change is
// pushed through `on_change` immediately (the skin toggle has to feel instant),
// so there is no OK/Apply ceremony to get in the way.
#pragma once

#include <functional>
#include <string>

#include "azy/core/settings.hpp"
#include "azy/win32/os/win_compat.hpp"

namespace azy {
namespace win {

class SettingsWindow {
public:
    struct Callbacks {
        std::function<void(const Settings&)> on_change;  // live apply
        std::function<void()> on_reset;                  // "Reset configuration"
        std::function<void()> on_open_log;               // "Open log file"
        std::function<void()> on_hidden;
    };

    struct Status {
        std::string host;         // "Windows 11 23H2 (build 22631)"
        std::string premiere;     // "Premiere Pro 2025 25.6.0.58 - active" / "not running"
        std::string treatment;    // "full", "reduced (...)", "safe mode"
        std::string safe_mode_note;
        unsigned long long surface_presents = 0;
        bool safe_mode = false;
    };

    ~SettingsWindow() { destroy(); }

    bool create(HINSTANCE instance, Callbacks callbacks, std::string* error);
    void destroy();

    void show(const Settings& settings, const Status& status);
    void hide();
    bool visible() const;

    // Refreshes an already-open window (live status text + control states) without
    // showing, activating or stealing focus - used when the tray menu or a
    // hand-edited settings.ini changes something while the window is open.
    void refresh(const Settings& settings, const Status& status, bool skin_enabled, bool suspended);
    HWND hwnd() const { return hwnd_; }

    // Updates the read-only parts (status lines) without touching user edits.
    void update_status(const Status& status, bool skin_enabled, bool suspended);

private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handle(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    void rebuild_controls();
    void layout(int dpi);
    void sync_controls();
    void push_settings();       // user edit -> callbacks_.on_change
    void update_slider_labels();
    void set_dark_theme();

    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    HFONT font_bold_ = nullptr;
    HFONT font_small_ = nullptr;
    HBRUSH background_ = nullptr;
    Callbacks callbacks_;
    Settings settings_;   // working copy owned by the window
    Status status_;
    int dpi_ = 96;
    bool creating_ = false;  // suppress change callbacks while filling controls
    bool skin_enabled_ = true;
    bool suspended_ = false;
};

}  // namespace win
}  // namespace azy
