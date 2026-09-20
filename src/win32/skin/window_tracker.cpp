#include "azy/win32/skin/window_tracker.hpp"

#include "azy/core/log.hpp"
#include "azy/core/ring_layout.hpp"
#include "azy/win32/detect/premiere_probe.hpp"
#include "azy/win32/os/win_api.hpp"
#include "azy/win32/os/win_util.hpp"

namespace azy {
namespace win {
namespace {

// Note: the settle delay after a move/resize lives in PerformanceManager, which
// owns the "should the skin be working right now" policy. This class only
// records geometry and tells the caller whether a re-apply is required.

// Visible-window counting walks the top level window list, so it is refreshed at
// most once per second (it only feeds diagnostics).
constexpr double kWindowCountIntervalSeconds = 1.0;

bool window_is_foreground(HWND hwnd, unsigned long pid) {
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr) return false;
    if (foreground == hwnd) return true;
    DWORD foreground_pid = 0;
    GetWindowThreadProcessId(foreground, &foreground_pid);
    return static_cast<unsigned long>(foreground_pid) == pid;
}

}  // namespace

bool WindowTracker::set_window(HWND hwnd, unsigned long pid) {
    if (hwnd == nullptr || !IsWindow(hwnd)) return false;

    target_ = SkinTarget{};
    target_.hwnd = hwnd;
    target_.pid = pid;
    target_.thread_id = GetWindowThreadProcessId(hwnd, nullptr);
    target_.window_class = window_class_name(hwnd);

    has_applied_ = false;
    needs_apply_ = true;
    last_window_count_check_ = 0.0;
    was_minimized_ = false;
    clamp_logged_ = false;

    // Fill the snapshot immediately so the first apply has real geometry.
    RefreshResult result = refresh(monotonic_seconds());
    (void)result;

    log_info("window tracker: target 0x%p (%s), pid %lu, thread %lu, class '%s'",
             reinterpret_cast<void*>(hwnd), target_.maximized ? "maximized" : "windowed", target_.pid,
             target_.thread_id, to_utf8(target_.window_class).c_str());
    return true;
}

void WindowTracker::clear() {
    target_ = SkinTarget{};
    has_applied_ = false;
    needs_apply_ = true;
    clamp_logged_ = false;
}

WindowTracker::RefreshResult WindowTracker::refresh(double now) {
    RefreshResult result;
    if (target_.hwnd == nullptr) {
        result.window_gone = true;
        return result;
    }
    if (!IsWindow(target_.hwnd)) {
        result.window_gone = true;
        return result;
    }

    RECT frame{};
    if (!GetWindowRect(target_.hwnd, &frame)) {
        result.window_gone = true;
        return result;
    }

    RECT visible{};
    const bool have_visible = visible_frame_rect(target_.hwnd, visible);
    const Rect new_frame = to_rect(frame);
    Rect new_visible = have_visible ? to_rect(visible) : new_frame;  // ring_frame() may replace it below

    const bool minimized = is_window_minimized(target_.hwnd);
    const bool maximized = is_window_maximized(target_.hwnd);
    const bool cloaked = is_window_cloaked(target_.hwnd);

    HMONITOR monitor = MonitorFromWindow(target_.hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);
    Rect monitor_rect = new_visible;
    Rect work_area = new_visible;
    if (monitor && GetMonitorInfoW(monitor, &monitor_info)) {
        monitor_rect = to_rect(monitor_info.rcMonitor);
        work_area = to_rect(monitor_info.rcWork);
    }

    const UINT dpi = win::api().get_dpi_for_window ? win::dpi_for_window(target_.hwnd)
                                                   : win::dpi_for_rect(frame);

    // Windows places a maximized window so that its invisible resize border hangs
    // over the monitor edges, and the numbers we read back can therefore start at
    // a negative coordinate. The ring is drawn inside the frame edge, so those
    // numbers would put the entire treatment off the display - the skin would run
    // perfectly and show nothing. Draw on what is visible instead (see ring_frame).
    const bool fullscreen_raw = !minimized && !maximized && new_visible == monitor_rect;
    const Rect reported_visible = new_visible;
    new_visible = ring_frame(new_visible, monitor_rect, work_area, maximized, fullscreen_raw);
    if (maximized && new_visible != reported_visible && !clamp_logged_) {
        clamp_logged_ = true;
        log_info("ring frame: maximized window reports (%d,%d)-(%d,%d), which hangs over the display; "
                 "drawing on the visible work area (%d,%d)-(%d,%d) instead",
                 reported_visible.left, reported_visible.top, reported_visible.right, reported_visible.bottom,
                 new_visible.left, new_visible.top, new_visible.right, new_visible.bottom);
    }

    const bool geometry_changed = rect_changed(target_.visible_frame, new_visible, 0) ||
                                  rect_changed(target_.frame, new_frame, 0);
    const bool state_changed = minimized != target_.minimized || maximized != target_.maximized ||
                               cloaked != target_.cloaked || dpi != target_.dpi ||
                               monitor_rect != target_.monitor;

    target_.frame = new_frame;
    target_.visible_frame = new_visible;
    target_.monitor = monitor_rect;
    target_.work_area = work_area;
    target_.dpi = dpi != 0 ? dpi : 96;
    target_.minimized = minimized;
    target_.maximized = maximized;
    target_.cloaked = cloaked;
    target_.fullscreen = fullscreen_raw;
    target_.foreground = window_is_foreground(target_.hwnd, target_.pid);

    if (now - last_window_count_check_ >= kWindowCountIntervalSeconds) {
        last_window_count_check_ = now;
        int count = 0;
        for (HWND window : PremiereProbe::find_top_level_windows(target_.pid)) {
            if (IsWindowVisible(window) && !is_window_cloaked(window)) ++count;
        }
        target_.window_count = count;
    }

    if (geometry_changed) {
        last_geometry_change_ = now;
        result.changed = true;
        // While the user is dragging or the window is moving, Azy keeps its own
        // surface out of the way (see PerformanceManager); the geometry is still
        // recorded so the next apply is exact.
        if (!rect_changed(applied_visible_, new_visible, 0)) {
            // Snap-back to the last applied position: nothing to do.
            result.needs_apply = false;
            return result;
        }
        needs_apply_ = true;
    }

    if (state_changed) {
        result.changed = true;
        needs_apply_ = true;
        if (minimized != was_minimized_) {
            log_info("Premiere %s", minimized ? "minimized - pausing skin work" : "restored - resuming skin work");
            was_minimized_ = minimized;
        }
        if (dpi != applied_dpi_ && has_applied_) {
            log_info("Premiere moved to %u DPI (%d%%) - rescaling skin", dpi, static_cast<int>(dpi * 100 / 96));
        }
    }

    result.needs_apply = needs_apply_;
    return result;
}

void WindowTracker::mark_applied() {
    applied_visible_ = target_.visible_frame;
    applied_dpi_ = target_.dpi;
    has_applied_ = true;
    needs_apply_ = false;
}

double WindowTracker::seconds_since_geometry_change(double now) const {
    if (last_geometry_change_ <= 0.0) return 1000.0;
    return now - last_geometry_change_;
}

}  // namespace win
}  // namespace azy
