#include "azy/win32/detect/premiere_detector.hpp"
#include <string>
#include <utility>
#include <vector>

#include "azy/core/log.hpp"
#include "azy/core/strings.hpp"
#include "azy/win32/os/win_util.hpp"

namespace azy {
namespace win {
namespace {

const std::wstring& headless_name() {
    static const std::wstring name = L"adobe premiere pro headless.exe";
    return name;
}

}  // namespace

bool PremiereDetector::start(Callback callback, std::string* error) {
    callback_ = std::move(callback);

    // Optional but preferred: instant process start/stop notifications.
    std::string wmi_error;
    if (!scanner_.start_events(premiere_executable_names_lower(), &wmi_error)) {
        log_info("process observer: WMI events unavailable (%s); using window events + on-demand scans",
                 wmi_error.c_str());
    }

    force_rescan(0.0);
    return true;
}

void PremiereDetector::stop() {
    scanner_.shutdown();
    callback_ = nullptr;
    has_target_ = false;
    window_ = nullptr;
    record_ = ProcessRecord{};
}

void PremiereDetector::note_window_activity() {
    pending_scan_ = true;
}

void PremiereDetector::force_rescan(double now) {
    pending_scan_ = true;
    pending_is_process_event_ = true;
    last_scan_ = -1000.0;
    rescan(now);
}

HWND PremiereDetector::current_window_for(unsigned long pid) const {
    return PremiereProbe::find_main_window(pid);
}

void PremiereDetector::pump(double now) {
    // 1. Anything from WMI?
    ProcessEvent process_event;
    if (scanner_.poll_event(process_event)) {
        ++stats_.events_from_wmi;
        pending_scan_ = true;
        pending_is_process_event_ = true;
        log_debug("process event: %s %lu (pid %lu)", process_event.name.c_str(),
                  process_event.pid, process_event.pid);
    }

    if (!pending_scan_) return;

    // A process start/stop is worth handling immediately; pure window activity
    // is throttled so a burst of events (docking panels, opening a menu) turns
    // into one rescan instead of dozens.
    // A process start/stop is worth handling immediately; pure window activity is
    // throttled so a burst of events (docking panels, opening a menu) turns into one
    // rescan instead of dozens - and, when nothing is being tracked yet, throttled
    // further still: see kIdleScanIntervalSeconds.
    const double minimum_interval = pending_is_process_event_ ? 0.0
                                    : has_target_            ? kMinScanIntervalSeconds
                                                             : kIdleScanIntervalSeconds;
    if (last_scan_ >= 0.0 && (now - last_scan_) < minimum_interval) return;

    last_scan_ = now;
    pending_scan_ = false;
    pending_is_process_event_ = false;
    rescan(now);
}

void PremiereDetector::rescan(double now) {
    (void)now;
    ++stats_.scans;

    std::vector<unsigned long> pids = scanner_.find_processes(premiere_executable_names_lower());

    // Drop the headless/encoder helper: it has no user interface to skin, but it
    // still counts as "Premiere is installed and running" for the log.
    std::vector<unsigned long> skin_candidates;
    for (unsigned long pid : pids) {
        ProcessRecord probe;
        if (!PremiereProbe::read_identity(pid, probe)) continue;
        if (iequals_wide(probe.exe_name, headless_name())) continue;
        skin_candidates.push_back(pid);
    }

    if (skin_candidates.empty()) {
        if (has_target_) {
            log_info("Premiere Pro closed (pid %lu) - releasing skin resources", record_.pid);
            has_target_ = false;
            window_ = nullptr;
            const ProductInfo previous = record_.product;
            record_ = ProcessRecord{};
            if (callback_) {
                Event event;
                event.kind = EventKind::Stopped;
                event.product = previous;
                callback_(event);
            }
        }
        return;
    }

    // Already tracking a live candidate? Then only the window can have changed.
    bool tracking_live = false;
    for (unsigned long pid : skin_candidates) {
        if (has_target_ && pid == record_.pid) tracking_live = true;
    }

    if (tracking_live) {
        // The window we already have still exists: keep it. This deliberately does
        // not re-resolve on "not visible right now", because a minimized (or
        // hidden) Premiere must stay attached - suspending is the performance
        // manager's job, and dropping the target here would flash the frame back
        // to its original colour on every minimize/restore cycle.
        if (window_ != nullptr && IsWindow(window_)) return;

        window_ = nullptr;
        const HWND new_window = current_window_for(record_.pid);
        if (new_window != nullptr) {
            window_ = new_window;
            log_info("Premiere main window changed (0x%p)", reinterpret_cast<void*>(window_));
            publish(EventKind::Changed, window_);
        }
        return;
    }

    // New process (or a different one than the tracked build): identify it.
    unsigned long chosen = 0;
    HWND chosen_window = nullptr;
    for (unsigned long pid : skin_candidates) {
        const HWND candidate_window = current_window_for(pid);
        // Prefer a process that already owns a real window.
        if (candidate_window != nullptr) {
            chosen = pid;
            chosen_window = candidate_window;
            break;
        }
        if (chosen == 0) chosen = pid;
    }
    if (chosen == 0) return;

    ProcessRecord probe;
    if (!PremiereProbe::read_identity(chosen, probe)) return;

    const bool was_tracking = has_target_;
    record_ = probe;
    window_ = chosen_window;
    has_target_ = true;

    log_info("Premiere detected: %s, version %s, pid %lu", probe.product.display_name().c_str(),
             probe.product.version_string().c_str(), probe.pid);
    if (probe.version_known) {
        log_info("Premiere version %s (major %d, family %s, channel %s)", probe.product.version_string().c_str(),
                 probe.file_version.major, product_family_name(probe.product.family),
                 probe.product.channel == ProductChannel::Beta ? "beta" : "release");
    } else {
        log_warn("Premiere VERSIONINFO unavailable - using conservative skin");
    }
    if (chosen_window == nullptr) {
        log_info("Premiere editor window not created yet; waiting for it");
    }
    publish(was_tracking ? EventKind::Changed : EventKind::Started, chosen_window);
}

void PremiereDetector::publish(EventKind kind, HWND window) {
    if (!callback_) return;
    Event event;
    event.kind = kind;
    event.product = record_.product;
    event.pid = record_.pid;
    event.window = window;
    event.window_ready = window != nullptr;
    callback_(event);
}

}  // namespace win
}  // namespace azy
