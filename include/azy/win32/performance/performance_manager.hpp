// Azy Skin — Win32 layer: PerformanceManager.
//
// Single place that decides whether the skin should be doing anything right
// now, and how often Azy's timer needs to run. Suspend reasons are explicit so
// the tray tooltip and the log never have to guess, and so "why is my Premiere
// not skinned?" has a one-line answer.
#pragma once

#include <string>

#include "azy/win32/skin/skin_types.hpp"

namespace azy {
namespace win {

class PerformanceManager {
public:
    struct Inputs {
        bool skin_enabled = true;
        bool original_theme = false;
        bool has_window = false;
        bool window_minimized = false;
        bool window_cloaked = false;
        bool window_foreground = false;
        bool move_size_loop = false;         // Windows is in a modal move/size loop
        double seconds_since_move = 1000.0;  // time since the last geometry change
        bool suspend_on_minimize = true;
        bool suspend_on_inactive = false;
        bool performance_mode = false;
    };

    struct Decision {
        bool suspend = true;
        SuspendReason reason = SuspendReason::NoWindow;
        // Backstop cadence for the safety-net timer, not a polling frequency:
        // the skin reacts to Windows notifications, not to this timer.
        int timer_interval_ms = 1000;
    };

    Decision evaluate(const Inputs& inputs);

    // Diagnostics: seconds since the last time the skin actually changed
    // something. Used by the settings window ("idle for Xs").
    void note_activity(double now) { last_activity_ = now; }
    double seconds_idle(double now) const { return last_activity_ <= 0.0 ? 0.0 : now - last_activity_; }
    bool suspended() const { return last_decision_.suspend; }
    const Decision& last_decision() const { return last_decision_; }

private:
    Decision last_decision_;
    double last_activity_ = 0.0;
};

}  // namespace win
}  // namespace azy
