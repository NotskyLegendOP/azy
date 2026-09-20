// Azy Skin — Win32 layer: global, event-driven UI observer.
//
// Azy never polls Premiere for "is anything happening": Windows tells us. This
// class owns three SetWinEventHook registrations (WINEVENT_OUTOFCONTEXT, so no
// DLL is injected into any process) and reduces everything to a handful of
// atomic flags that the application drains on its low-frequency timer.
//
// Callbacks arrive on the thread that registered the hook, which is the
// application's UI thread. The handler itself does nothing but compare integers
// and set flags, so being called on the UI thread cannot stall Premiere or Azy.
#pragma once

#include <atomic>
#include <functional>
#include <string>

#include "azy/win32/os/win_compat.hpp"

namespace azy {
namespace win {

class EventWatch {
public:
    ~EventWatch() { stop(); }

    // `class_name_filter` limits delivery to windows of that class when set
    // (empty = every window). Registration never fails hard: the caller can
    // fall back to its timer-only safety net.
    bool start(const std::wstring& class_name_filter, std::string* error);
    void stop();
    bool active() const { return primary_hook_ != nullptr || object_hook_ != nullptr; }

    // Any relevant change at all (window created/destroyed/shown/moved/etc.).
    bool consume_dirty();
    // Movement/resize of the watched process' windows specifically.
    bool consume_location_dirty();
    // Foreground window changed (or a desktop switch happened).
    bool consume_foreground_dirty();

    // True while Windows is inside a modal move/size loop (the user is dragging
    // the Premiere window). Azy suspends its composition surface for the whole
    // loop instead of chasing the window pixel by pixel.
    bool in_move_size_loop() const { return move_size_loop_.load() != 0; }

    // Relevance filtering. `thread` is the fast path (a pure integer compare
    // against the event's owning thread, no syscall); `pid` is the fallback used
    // when the target's window is recreated on a different thread. 0 = observe
    // everything, which is what Azy does while it is waiting for Premiere.
    void set_watched_thread(unsigned long thread_id) { watched_thread_.store(thread_id); }
    unsigned long watched_thread() const { return watched_thread_.load(); }
    void set_watched_pid(unsigned long pid) { watched_pid_.store(pid); }
    unsigned long watched_pid() const { return watched_pid_.load(); }

    // Pause delivery entirely (used while the skin is off and no target exists,
    // so Azy is genuinely idle in the background).
    void set_paused(bool paused) { paused_.store(paused); }

    unsigned long long event_count() const { return event_count_.load(); }
    void set_debug_logging(bool enabled) { debug_logging_.store(enabled); }

    // Immediate reaction without doing work inside a WinEvent callback: the
    // application supplies a function that posts itself a message. Coalescing is
    // deliberately left to that function (AppController::post_sync keeps a single
    // pending flag), so a burst of 200 events still produces exactly one wake-up.
    void set_notify(std::function<void()> notify) { notify_ = std::move(notify); }

    // Used when a modal move/size loop ended without its end notification.
    void clear_move_size_loop() { move_size_loop_.store(0); }

private:
    static void CALLBACK on_event(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG id_object, LONG id_child,
                                  DWORD event_thread, DWORD event_time);
    void handle(DWORD event, HWND hwnd, LONG id_object, DWORD event_thread);
    bool relevant(DWORD event, HWND hwnd, DWORD event_thread) const;

    HWINEVENTHOOK primary_hook_ = nullptr;   // system events
    HWINEVENTHOOK object_hook_ = nullptr;    // window lifecycle + geometry
    HWINEVENTHOOK cloak_hook_ = nullptr;     // DWM cloak/uncloak

    std::wstring class_filter_;
    std::atomic<unsigned long> watched_thread_{0};
    std::atomic<unsigned long> watched_pid_{0};
    std::atomic<bool> paused_{false};
    std::atomic<bool> debug_logging_{false};
    std::atomic<bool> dirty_{false};
    std::atomic<bool> location_dirty_{false};
    std::atomic<bool> foreground_dirty_{false};
    std::atomic<int> move_size_loop_{0};
    std::atomic<unsigned long long> event_count_{0};
    std::function<void()> notify_;  // set once, before events can arrive
};

}  // namespace win
}  // namespace azy
