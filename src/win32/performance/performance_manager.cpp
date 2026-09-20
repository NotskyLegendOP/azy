#include "azy/win32/performance/performance_manager.hpp"

namespace azy {
namespace win {
namespace {

// Timer cadence. This timer is a *safety net*, not a polling loop: the skin is
// driven by WinEvent/WMI notifications (which post an immediate sync message),
// so a tick normally does nothing but read a few atomics. The intervals below
// only decide how quickly Azy recovers from a change Windows did not report.
constexpr int kIntervalSettledMs = 1000;
constexpr int kIntervalSuspendedMs = 2000;
constexpr int kIntervalPerformanceMs = 1000;

// After a move/resize, hold off briefly: it swallows the tail of the event
// stream instead of ending with a 1px correction nobody can see.
constexpr double kMoveHysteresisSeconds = 0.15;

}  // namespace

PerformanceManager::Decision PerformanceManager::evaluate(const Inputs& inputs) {
    Decision decision;

    if (!inputs.skin_enabled || inputs.original_theme) {
        decision.suspend = true;
        decision.reason = inputs.skin_enabled ? SuspendReason::OriginalTheme : SuspendReason::SkinDisabled;
        decision.timer_interval_ms = kIntervalSuspendedMs;
        last_decision_ = decision;
        return decision;
    }

    if (!inputs.has_window) {
        decision.suspend = true;
        decision.reason = SuspendReason::NoWindow;
        decision.timer_interval_ms = kIntervalSettledMs;
        last_decision_ = decision;
        return decision;
    }

    if (inputs.window_minimized && inputs.suspend_on_minimize) {
        // Minimized: the composition surface is hidden and DWM work is skipped.
        // Restoring is noticed through EVENT_SYSTEM_MINIMIZEEND, which is
        // instant, so nothing has to be polled for while Premiere sits in the
        // taskbar.
        decision.suspend = true;
        decision.reason = SuspendReason::Minimized;
        decision.timer_interval_ms = kIntervalSuspendedMs;
        last_decision_ = decision;
        return decision;
    }

    if (inputs.window_cloaked) {
        // Hidden by DWM (virtual desktop switch / shell transition): nothing to
        // draw on, and drawing would be wasted work.
        decision.suspend = true;
        decision.reason = SuspendReason::FullscreenTransition;
        decision.timer_interval_ms = kIntervalSuspendedMs;
        last_decision_ = decision;
        return decision;
    }

    if (!inputs.window_foreground && inputs.suspend_on_inactive) {
        decision.suspend = true;
        decision.reason = SuspendReason::Inactive;
        decision.timer_interval_ms = kIntervalSuspendedMs;
        last_decision_ = decision;
        return decision;
    }

    if (inputs.move_size_loop || inputs.seconds_since_move < kMoveHysteresisSeconds) {
        // The user is dragging or resizing right now. The DWM window attributes
        // need no help (Windows moves them with the window), and Azy's own
        // surface stays out of the way until the movement settles.
        decision.suspend = true;
        decision.reason = inputs.move_size_loop ? SuspendReason::Dragging : SuspendReason::Moving;
        decision.timer_interval_ms = kIntervalSettledMs;
        last_decision_ = decision;
        return decision;
    }

    decision.suspend = false;
    decision.reason = SuspendReason::None;
    decision.timer_interval_ms = inputs.performance_mode ? kIntervalPerformanceMs : kIntervalSettledMs;
    last_decision_ = decision;
    return decision;
}

}  // namespace win
}  // namespace azy
