#include "azy/win32/watch/event_watch.hpp"
#include <string>

#include "azy/core/log.hpp"
#include "azy/win32/os/win_util.hpp"

namespace azy {
namespace win {
namespace {

// Event constants (winuser.h) spelled out so the file reads as a list of what we
// actually care about, and so the ranges below stay tight: a narrower range
// means fewer callbacks for Windows to deliver.
constexpr DWORD kEventSystemForeground = 0x0003;
constexpr DWORD kEventSystemMoveSizeStart = 0x000A;
constexpr DWORD kEventSystemMoveSizeEnd = 0x000B;
constexpr DWORD kEventSystemMinimizeStart = 0x0016;
constexpr DWORD kEventSystemMinimizeEnd = 0x0017;
constexpr DWORD kEventSystemDesktopSwitch = 0x0020;
// The object range is registered as one hook (0x8000..0x800B) because those are
// the only ones with a cheap, useful meaning here:
//   0x8000 create, 0x8001 destroy, 0x8002 show, 0x8003 hide,
//   0x800A state change, 0x800B location change (move/resize)
constexpr DWORD kEventObjectCreate = 0x8000;
constexpr DWORD kEventObjectDestroy = 0x8001;
constexpr DWORD kEventObjectLocationChange = 0x800B;
constexpr DWORD kEventObjectCloaked = 0x8017;
constexpr DWORD kEventObjectUncloaked = 0x8018;
constexpr LONG kObjIdWindow = 0;

EventWatch* g_active_watch = nullptr;  // WinEvent callbacks carry no user data

}  // namespace

bool EventWatch::start(const std::wstring& class_name_filter, std::string* error) {
    if (primary_hook_ || object_hook_) return true;

    class_filter_ = class_name_filter;
    g_active_watch = this;

    const DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;

    // 1. System events: foreground changes, minimize, move/size loops.
    primary_hook_ = SetWinEventHook(kEventSystemForeground, kEventSystemMoveSizeEnd, nullptr, &on_event, 0, 0, flags);
    if (!primary_hook_) {
        if (error) *error = "SetWinEventHook(system) failed: " + to_utf8(last_error_text());
        g_active_watch = nullptr;
        return false;
    }

    // 2. Window lifecycle + geometry. This is the range that makes tracking
    //    event-driven instead of polled.
    object_hook_ = SetWinEventHook(kEventObjectCreate, kEventObjectLocationChange, nullptr, &on_event, 0, 0, flags);
    if (!object_hook_) {
        if (error) *error = "SetWinEventHook(object) failed: " + to_utf8(last_error_text());
        UnhookWinEvent(primary_hook_);
        primary_hook_ = nullptr;
        g_active_watch = nullptr;
        return false;
    }

    // 3. Cloak transitions (virtual desktop / suspended shell windows). Failure
    //    here is harmless: cloak state is also checked when a target is applied.
    cloak_hook_ = SetWinEventHook(kEventObjectCloaked, kEventObjectUncloaked, nullptr, &on_event, 0, 0, flags);

    // A desktop switch means every geometry we know may be stale.
    dirty_.store(true);
    log_info("WinEvent observer started (event-driven window tracking)");
    return true;
}

void EventWatch::stop() {
    if (primary_hook_) UnhookWinEvent(primary_hook_);
    if (object_hook_) UnhookWinEvent(object_hook_);
    if (cloak_hook_) UnhookWinEvent(cloak_hook_);
    primary_hook_ = nullptr;
    object_hook_ = nullptr;
    cloak_hook_ = nullptr;
    if (g_active_watch == this) g_active_watch = nullptr;
}

bool EventWatch::consume_dirty() { return dirty_.exchange(false); }
bool EventWatch::consume_location_dirty() { return location_dirty_.exchange(false); }
bool EventWatch::consume_foreground_dirty() { return foreground_dirty_.exchange(false); }

void CALLBACK EventWatch::on_event(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG id_object, LONG /*id_child*/,
                                   DWORD event_thread, DWORD) {
    EventWatch* self = g_active_watch;
    if (self == nullptr) return;
    // Object events for anything below a top-level window (carets, child
    // controls, text) are irrelevant to the skin and extremely chatty.
    if (event >= 0x8000 && id_object != kObjIdWindow) return;
    self->handle(event, hwnd, id_object, event_thread);
}

// Decides whether an event can affect the skin. The common case is a single
// integer comparison against the target's window thread; only unowned or
// re-created windows cost one GetWindowThreadProcessId call.
bool EventWatch::relevant(DWORD event, HWND hwnd, DWORD event_thread) const {
    const bool is_system_event = event < 0x8000;
    if (is_system_event) {
        switch (event) {
            case kEventSystemForeground:
            case kEventSystemMoveSizeStart:
            case kEventSystemMoveSizeEnd:
            case kEventSystemDesktopSwitch:
                return true;
            default:
                break;
        }
    }

    const unsigned long watched_thread = watched_thread_.load();
    const unsigned long watched_pid = watched_pid_.load();
    if (watched_thread == 0 && watched_pid == 0) return true;  // idle: observe everything

    if (watched_thread != 0 && event_thread == watched_thread) return true;
    if (watched_pid != 0 && hwnd != nullptr) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (static_cast<unsigned long>(pid) == watched_pid) return true;
    }
    return false;
}

void EventWatch::handle(DWORD event, HWND hwnd, LONG /*id_object*/, DWORD event_thread) {
    event_count_.fetch_add(1);
    if (paused_.load()) return;
    if (!relevant(event, hwnd, event_thread)) return;

    // Wake the application. The callback coalesces (it only posts a message when
    // one is not already pending), so this is a cheap function call, not a
    // message storm.
    if (notify_) notify_();

    switch (event) {
        case kEventSystemMoveSizeStart:
            move_size_loop_.fetch_add(1);
            location_dirty_.store(true);
            dirty_.store(true);
            return;
        case kEventSystemMoveSizeEnd:
            move_size_loop_.store(0);
            location_dirty_.store(true);
            dirty_.store(true);
            return;
        case kEventSystemDesktopSwitch:
            dirty_.store(true);
            location_dirty_.store(true);
            foreground_dirty_.store(true);
            return;
        case kEventSystemForeground:
            foreground_dirty_.store(true);
            dirty_.store(true);
            break;
        case kEventSystemMinimizeStart:
        case kEventSystemMinimizeEnd:
            dirty_.store(true);
            location_dirty_.store(true);
            return;
        default:
            break;
    }

    if (!class_filter_.empty() && hwnd != nullptr) {
        if (!iequals_wide(window_class_name(hwnd), class_filter_)) return;
    }

    dirty_.store(true);
    if (event == kEventObjectLocationChange || event == kEventObjectCreate || event == kEventObjectDestroy) {
        location_dirty_.store(true);
    }
}

}  // namespace win
}  // namespace azy
