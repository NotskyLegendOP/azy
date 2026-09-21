// Azy Skin — application layer: the orchestrator.
//
// Owns one hidden message window (the only window Azy has when idle) and wires
// the modules together:
//
//   EventWatch / WMI  ->  PremiereDetector  ->  WindowTracker
//                                                  |
//                             PerformanceManager  ->  SkinEngine  ->  the mirror
//
// Everything is driven by Windows notifications; the timer is a backstop only.
#pragma once

#include <functional>
#include <string>
#include <vector>

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
    // Asked by a *newer* build that wants to take over from this instance (see
    // main.cpp): the process that owns the skin is the one that has to give it up.
    static UINT exit_request_message_id();
    // A running instance answers this with its version (see packed_version), so a
    // freshly started build can tell "the user opened Azy again" from "an older
    // build is still skinning Premiere and this launch would be a no-op".
    static UINT version_query_message_id();
    // Version packed for comparison across processes (see azy::pack_version).
    static unsigned packed_version();

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
    // Restarts Azy with administrator rights (one UAC prompt) so it can draw above a
    // Premiere that is running elevated - the one situation Azy cannot work around.
    void restart_elevated();
    // Measures the composed desktop and reports what it found, so that "the skin
    // does nothing" can be answered with a measurement instead of a guess. Returns
    // the report text for the settings window to display.
    std::string check_visibility();
    std::string status_line() const;
    // One word for what is actually on screen right now: "active", "partial",
    // "idle" or the reason the skin is suspended. The tray tooltip, the settings
    // window headline and the log all use it, so none of them can claim more than
    // the engine really did.
    std::string state_summary() const;
    std::string premiere_summary() const;
    // Every fact the debug screen asks for (spec §38), shown in the settings window
    // and logged on request: "the skin does nothing" can then be reported with a
    // screenshot instead of a log file.
    std::vector<std::string> diagnostics_lines() const;
    std::string process_cpu_line() const;
    // Frames per second actually observed, from two diagnostics reads. Honest
    // about the first read, which has nothing to compare against.
    std::string measured_rates_line(unsigned long long frames, unsigned long long presents) const;
    // The one-line description of what the skin is doing, for the tooltip and the
    // settings window headline.
    std::string treatment_summary() const;

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
    // The duplicate window's state, remembered only so that a change is logged
    // once (the same "log transitions, not ticks" rule as everywhere else).
    bool mirror_active_ = false;
    bool mirror_capturing_ = false;
    unsigned long long failures_seen_ = 0;
    bool safe_mode_ = false;
    bool premiere_elevated_ = false;  // Premiere runs at a higher integrity level than Azy
    bool mirror_warning_shown_ = false;  // the "nothing is on screen" notice was already shown
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
