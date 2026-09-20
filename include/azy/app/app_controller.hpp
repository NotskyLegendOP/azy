// Azy Skin — application layer: the orchestrator.
//
// Owns one hidden message window (the only window Azy has when idle) and wires
// the modules together:
//
//   EventWatch / WMI  ->  PremiereDetector  ->  WindowTracker
//                                                  |
//                             PerformanceManager  ->  SkinEngine  ->  DWM + surface
//
// Everything is driven by Windows notifications; the timer is a backstop only.
#pragma once

#include <functional>
#include <string>

#include "azy/app/app_settings_store.hpp"
#include "azy/core/failure_tracker.hpp"
#include "azy/win32/detect/premiere_detector.hpp"
#include "azy/win32/os/file_watcher.hpp"
#include "azy/win32/performance/performance_manager.hpp"
#include "azy/win32/skin/skin_engine.hpp"
#include "azy/win32/skin/window_tracker.hpp"
#include "azy/win32/ui/settings_window.hpp"
#include "azy/win32/ui/tray.hpp"
#include "azy/win32/watch/event_watch.hpp"

namespace azy {
namespace app {

struct CommandLine {
    bool tray_only = false;      // --tray (also passed by the Run entry)
    bool open_settings = false;  // --settings
    bool reset_settings = false; // --reset
    bool verbose_logging = false;  // --debug
    bool no_tray = false;        // --no-tray (diagnostics: no shell integration)
};

class AppController {
public:
    AppController() = default;
    ~AppController() = default;

    bool initialize(HINSTANCE instance, const CommandLine& command_line, std::string* error);
    int run();

    // Posts the sync message; safe to call from a WinEvent callback (coalesced).
    void post_sync();
    void request_exit(int exit_code = 0);

    // Message ids used by the message window and by the watchers.
    static UINT sync_message_id();
    static UINT settings_changed_message_id();
    static UINT open_settings_message_id();

private:
    static LRESULT CALLBACK message_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    bool create_message_window(std::string* error);
    LRESULT handle_message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    void start_observers();
    void stop_observers();

    void sync(const char* reason);
    void on_premiere_event(const win::PremiereDetector::Event& event);
    void on_settings_changed_on_disk();

    void compute_request(win::SkinRequest& request, win::SuspendReason& reason) const;
    FeatureSet effective_features(const ProductInfo& product) const;
    ThemePalette effective_palette(bool dark_frame_supported) const;

    void merge_user_settings(const Settings& incoming);
    void persist_failure_state();
    // Applies (and then deletes) the one-shot preferences an installer left
    // behind, so setup choices never overwrite a user's later edits.
    void apply_installer_defaults();
    void apply_settings_side_effects(const Settings& previous);
    void enter_safe_mode(const std::string& reason);
    void update_tray();
    void update_settings_window_status();
    void open_log_file() const;
    std::string status_line() const;
    std::string premiere_summary() const;
    std::string treatment_summary(const FeatureSet& features) const;

    // Sampling interval for window geometry while the user is dragging/resizing
    // Premiere (the surface is hidden during a drag, so ~10 Hz is plenty).
    static constexpr double kDragRefreshIntervalSeconds = 0.1;

    void arm_timer(int interval_ms);
    void shutdown();

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    CommandLine command_line_;

    SettingsStore store_;
    win::EventWatch watch_;
    win::PremiereDetector detector_;
    win::WindowTracker tracker_;
    win::PerformanceManager performance_;
    win::SkinEngine engine_;
    win::TrayIcon tray_;
    win::SettingsWindow settings_window_;
    win::DirectoryWatcher settings_watcher_;
    FailureTracker failures_;

    ProductInfo product_;
    win::SkinState engine_state_;
    unsigned long long failures_seen_ = 0;
    bool safe_mode_ = false;
    bool manual_apply_ = false;      // user asked for the skin although autostart is off
    bool suspended_manual_ = false;  // tray "Suspend Skin"
    bool skin_active_last_ = false;
    bool shutting_down_ = false;
    bool sync_pending_ = false;
    int exit_code_ = 0;
    int timer_interval_ = 0;
    double last_tracker_refresh_ = 0.0;
    std::string last_status_line_;
    std::string premiere_status_;
};

}  // namespace app
}  // namespace azy
