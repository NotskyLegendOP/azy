#include "azy/core/failure_tracker.hpp"

#include "azy/core/strings.hpp"

namespace azy {

bool FailureTracker::record_failure(double now) {
    // First failure (or a failure after a long quiet period) opens a new window.
    if (failures_ == 0 || (now - window_start_) > window_seconds_) {
        window_start_ = now;
        failures_ = 0;
    }
    last_failure_ = now;
    ++failures_;
    if (failures_ >= threshold_) {
        tripped_ = true;
        return true;
    }
    return false;
}

void FailureTracker::record_success(double now) {
    if (failures_ == 0) return;
    if ((now - last_failure_) > window_seconds_) {
        // Quiet for a whole window: forget the history.
        failures_ = 0;
        window_start_ = 0.0;
    }
}

bool FailureTracker::consume_trip() {
    if (!tripped_) return false;
    tripped_ = false;
    failures_ = 0;
    window_start_ = 0.0;
    return true;
}

void FailureTracker::reset() {
    failures_ = 0;
    window_start_ = 0.0;
    last_failure_ = 0.0;
    tripped_ = false;
}

std::string FailureTracker::serialize() const {
    return str_format("%d|%.3f|%.3f|%d", failures_, window_start_, last_failure_, tripped_ ? 1 : 0);
}

FailureTracker FailureTracker::deserialize(const std::string& text, int threshold, double window_seconds) {
    FailureTracker tracker(threshold, window_seconds);
    const auto parts = split(text, '|');
    if (parts.size() >= 3) {
        int failures = 0;
        if (parse_int(parts[0], failures)) tracker.failures_ = failures < 0 ? 0 : failures;
        double start = 0.0, last = 0.0;
        try {
            if (!trim(parts[1]).empty()) start = std::stod(trim(parts[1]));
            if (!trim(parts[2]).empty()) last = std::stod(trim(parts[2]));
        } catch (...) {
            start = 0.0;
            last = 0.0;
        }
        tracker.window_start_ = start;
        tracker.last_failure_ = last;
        if (parts.size() >= 4) {
            bool tripped = false;
            if (parse_bool(parts[3], tripped)) tracker.tripped_ = tripped;
        }
    }
    return tracker;
}

}  // namespace azy
