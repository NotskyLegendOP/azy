// Azy Skin — portable core: failure tracking for automatic Safe Mode.
//
// A "failure" is a guarded engine operation that did not behave (a DWM call
// rejected, a composition surface that could not be created, an unexpected
// window structure). Repeated failures inside a short window mean this Premiere
// build/window is not something we understand, so Azy falls back to Safe Mode
// instead of retrying forever.
#pragma once

#include <string>

namespace azy {

class FailureTracker {
public:
    FailureTracker() = default;
    FailureTracker(int threshold, double window_seconds)
        : threshold_(threshold < 1 ? 1 : threshold), window_seconds_(window_seconds < 1.0 ? 1.0 : window_seconds) {}

    // Records a failure at `now` (monotonic or wall-clock seconds; must be
    // consistent between calls). Returns true exactly once, at the moment the
    // threshold is reached inside the sliding window.
    bool record_failure(double now);

    // Called when the engine completes a full apply cycle cleanly. Successes do
    // not erase history immediately: the counter only decays once the last
    // failure is older than the window, which stops a flapping Premiere window
    // from resetting the count on every other frame.
    void record_success(double now);

    // Returns true and clears the counter when the threshold was reached.
    bool consume_trip();

    int failures() const { return failures_; }
    bool tripped() const { return tripped_; }
    bool had_failure() const { return failures_ > 0; }
    double window_start() const { return window_start_; }
    double last_failure() const { return last_failure_; }

    void reset();

    // Persistence: "failures|window_start|last_failure|tripped".
    std::string serialize() const;
    static FailureTracker deserialize(const std::string& text, int threshold, double window_seconds);

    void set_failures(int failures) { failures_ = failures < 0 ? 0 : failures; }
    void set_tripped(bool t) { tripped_ = t; }
    void set_window_start(double t) { window_start_ = t; }

private:
    int threshold_ = 3;
    double window_seconds_ = 300.0;
    int failures_ = 0;
    double window_start_ = 0.0;
    double last_failure_ = 0.0;
    bool tripped_ = false;
};

}  // namespace azy
