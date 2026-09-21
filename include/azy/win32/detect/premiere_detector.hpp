// Azy Skin — Win32 layer: PremiereDetector.
//
// Owns the "is Premiere running, which build, which window" question. It is
// woken by real events (WMI process notifications, WinEvent window activity) and
// only then performs work; between events it does nothing at all.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "azy/core/product.hpp"
#include "azy/win32/detect/premiere_probe.hpp"
#include "azy/win32/detect/process_scanner.hpp"

namespace azy {
namespace win {

class PremiereDetector {
public:
    enum class EventKind {
        Started,  // a Premiere process appeared (window may still be null)
        Stopped,  // every Premiere process is gone
        Changed,  // same process, but its main window changed (or appeared)
    };

    struct Event {
        EventKind kind = EventKind::Started;
        ProductInfo product;
        unsigned long pid = 0;
        HWND window = nullptr;
        bool window_ready = false;
    };

    using Callback = std::function<void(const Event&)>;

    ~PremiereDetector() { stop(); }

    bool start(Callback callback, std::string* error);
    void stop();

    // Cheap: drains queued events and, when something actually changed, rescans.
    // `now` is monotonic seconds. Safe to call from the message timer.
    void pump(double now);

    // Marks the state as "might have changed" — wired to the WinEvent observer.
    void note_window_activity();

    void force_rescan(double now);

    bool has_target() const { return has_target_; }

    // Whether a window event is worth acting on at all.
    //
    // There are exactly two reasons to rescan because a window appeared:
    //   * a target is known but its editor window has not been created yet (the
    //     splash-screen case), or
    //   * no target is known and there is no process observer to say otherwise.
    // When the WMI observer is live and nothing is tracked, Premiere cannot start
    // without an event arriving, and scanning the process list on every window
    // event on the desktop is pure cost.
    bool wants_window_activity() const {
        if (has_target_) return window_ == nullptr;
        return !scanner_.events_available();
    }
    const ProcessRecord& record() const { return record_; }
    HWND window() const { return window_; }
    unsigned long pid() const { return record_.pid; }
    bool wmi_events_available() const { return scanner_.events_available(); }

    // Diagnostics used by the settings window.
    struct Stats {
        unsigned long long scans = 0;
        unsigned long long events_from_wmi = 0;
    };
    const Stats& stats() const { return stats_; }

private:
    void rescan(double now);
    void publish(EventKind kind, HWND window);
    HWND current_window_for(unsigned long pid) const;

    Callback callback_;
    ProcessScanner scanner_;
    ProcessRecord record_;
    HWND window_ = nullptr;
    bool has_target_ = false;
    bool pending_scan_ = false;
    bool pending_is_process_event_ = false;
    double last_scan_ = -1.0;
    // A rescan while a target is live but its window is missing: fast, because the
    // user is looking at a splash screen and expects the skin within a moment.
    static constexpr double kMinScanIntervalSeconds = 0.20;
    // A rescan when there is no target at all and no process observer to rely on:
    // only "Premiere just started" is at stake, and nobody is staring at a window
    // waiting for it. Slower, so a busy desktop cannot turn into a scan loop.
    static constexpr double kIdleScanIntervalSeconds = 1.0;
    Stats stats_;
};

}  // namespace win
}  // namespace azy
