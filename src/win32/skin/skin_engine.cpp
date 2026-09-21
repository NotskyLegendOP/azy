#include "azy/win32/skin/skin_engine.hpp"

#include <algorithm>
#include <cstdio>

#include "azy/core/log.hpp"
#include "azy/win32/os/win_util.hpp"

namespace azy {
namespace win {
namespace {

// The capture's pace while the mirror is being drawn. The active rate is the
// mirror's own (60), so a frame is never the thing that holds the picture back;
// when nothing is changing, the capture drops to the idle rate on its own.
constexpr unsigned kMirrorActiveFps = 60;
constexpr unsigned kMaxFailureBurst = 3;
constexpr unsigned long long kRetryBackoffMs = 4000;

}  // namespace

const char* mirror_state_name(MirrorState state) {
    switch (state) {
        case MirrorState::WaitingForPremiere: return "WAITING_FOR_PREMIERE";
        case MirrorState::PremiereFound: return "PREMIERE_FOUND";
        case MirrorState::WindowValidated: return "WINDOW_VALIDATED";
        case MirrorState::CaptureInitializing: return "CAPTURE_INITIALIZING";
        case MirrorState::CaptureActive: return "CAPTURE_ACTIVE";
        case MirrorState::MirrorActive: return "MIRROR_ACTIVE";
        case MirrorState::CaptureFailed: return "CAPTURE_FAILED";
        case MirrorState::Unsupported: return "UNSUPPORTED";
        case MirrorState::Suspended: return "SUSPENDED";
    }
    return "WAITING_FOR_PREMIERE";
}

bool SkinEngine::initialize(std::string* error) {
    std::string mirror_error;
    if (!mirror_.create(GetModuleHandleW(nullptr), &mirror_error)) {
        // Not fatal: Azy runs, the tray works, and the diagnostics say exactly why
        // there is no mirror. Nothing else in the application depends on this.
        last_error_ = mirror_error;
        note_ = "capture initialization failed: " + mirror_error;
        set_state(MirrorState::Unsupported, "the mirror window could not be created");
        unsupported_ = true;
        log_error("mirror unavailable: %s", mirror_error.c_str());
        if (error != nullptr) *error = mirror_error;
        return false;
    }
    mirror_.attach_capture(&capture_);
    set_state(MirrorState::WaitingForPremiere, "nothing to mirror yet");
    return true;
}

void SkinEngine::shutdown() {
    capture_.stop();
    mirror_.destroy();
    frames_seen_ = false;
    capture_attempted_ = false;
    attached_hwnd_ = nullptr;
    attached_pid_ = 0;
}

void SkinEngine::revert() { teardown_mirror("the skin was switched off"); }

void SkinEngine::retry_overlay() {
    // The manual retry: the answer may have changed (a driver update, a new Premiere
    // window), so the previous verdict is cleared.
    unsupported_ = false;
    failure_burst_ = 0;
    next_attempt_ms_ = 0;
    capture_attempted_ = false;
    place_again_ = true;
    if (mirror_.created() && mirror_.needs_recreate()) {
        // The manual retry is also the way back from a lost GPU device.
        recover_device("the manual retry");
        return;
    }
    mirror_.set_animations(style_.animations);
    if (!mirror_.created()) {
        std::string mirror_error;
        if (mirror_.create(GetModuleHandleW(nullptr), &mirror_error)) {
            mirror_.attach_capture(&capture_);
            unsupported_ = false;
            last_error_.clear();
            log_info("mirror: recreated on request");
        } else {
            unsupported_ = true;
            last_error_ = mirror_error;
            note_ = "capture initialization failed: " + mirror_error;
        }
    }
}

void SkinEngine::teardown_mirror(const char* reason) {
    const bool was_active = mirror_.visible() || capture_.running();
    capture_.stop();
    mirror_.hide();
    attached_hwnd_ = nullptr;
    attached_pid_ = 0;
    frames_seen_ = false;
    capture_attempted_ = false;
    target_ = nullptr;
    if (was_active) {
        log_info("mirror: released (%s)", reason == nullptr ? "teardown" : reason);
    }
    note_ = reason == nullptr ? "stopped" : reason;
}

void SkinEngine::set_state(MirrorState state, const char* why) {
    if (state_ == state) {
        if (why != nullptr) note_ = why;
        return;
    }
    // The transition is logged, not the steady state: that is what makes a log file
    // readable when something goes wrong (spec §39).
    log_info("mirror state: %s -> %s (%s)", mirror_state_name(state_), mirror_state_name(state),
             why == nullptr ? "" : why);
    state_ = state;
    if (why != nullptr) note_ = why;
}

MirrorRect SkinEngine::overlay_for(const SkinTarget& target) {
    const Rect overlay =
        overlay_rect(target.visible_frame, target.monitor, target.work_area, target.maximized, target.fullscreen);
    return MirrorRect{overlay.left, overlay.top, overlay.width(), overlay.height()};
}

bool SkinEngine::start_capture(const SkinRequest& request, std::string* error) {
    const CaptureStart result = capture_.start(request.target.hwnd, request.target.pid, mirror_.shared_device(),
                                               kMirrorActiveFps, error);
    if (result == CaptureStart::Started) {
        attached_hwnd_ = request.target.hwnd;
        attached_pid_ = request.target.pid;
        frames_seen_ = false;
        failure_burst_ = 0;
        return true;
    }
    if (result == CaptureStart::Unsupported) {
        unsupported_ = true;
        set_state(MirrorState::Unsupported, "this Windows build has no window capture API");
        return false;
    }
    if (result == CaptureStart::Minimized) {
        set_state(MirrorState::Suspended, "Premiere is minimized");
        return false;
    }
    if (error != nullptr && error->empty()) *error = capture_start_text(result);
    ++failure_burst_;
    if (failure_burst_ >= kMaxFailureBurst) {
        unsupported_ = true;
        set_state(MirrorState::Unsupported, "the capture failed repeatedly; the log has the reason");
    } else {
        next_attempt_ms_ = GetTickCount64() + kRetryBackoffMs * failure_burst_;
        set_state(MirrorState::CaptureFailed, mirror_.created() ? "capture failed; retrying" : "no mirror window");
    }
    return false;
}

bool SkinEngine::apply(const SkinRequest& request, SkinState& state_out) {

    // The style is rebuilt on every apply: it is arithmetic over a handful of
    // doubles, and it is what makes a settings change take effect on the next event.
    const ThemeTokens tokens = theme_tokens(request.appearance.theme, request.appearance.custom_accent);
    // The DIP-to-pixel conversion for anything the shader measures in pixels.
    const float dpi_scale = request.target.dpi != 0 ? static_cast<float>(request.target.dpi) / 96.0f : 1.0f;
    style_ = make_mirror_style(tokens, request.appearance, request.performance_mode, dpi_scale);
    mirror_.set_style(style_);

    // A lost GPU device is noticed by the presenter, not here, so the rebuild happens
    // on the next apply rather than inside the pump - one second of nothing at worst,
    // and a bounded number of attempts (three, the same budget the capture uses).
    if (mirror_.created() && mirror_.needs_recreate() && GetTickCount64() >= next_attempt_ms_) {
        recover_device("the GPU device was lost");
    }

    const bool style_wanted = style_.visible && !mirror_style_is_passthrough(style_);
    const bool allowed = request.skin_enabled && style_wanted && request.suspend == SuspendReason::None &&
                         request.target.valid() && mirror_.created() && !unsupported_;

    ++state_out.applies;
    sync_mirror(request, allowed, state_out);
    // `mirror_active` / `mirror_capturing` / `mirror_note` are written by
    // sync_mirror, which is the only place that knows what actually happened; the
    // rest of the snapshot is filled in here.
    state_out.suspend = request.suspend;
    state_out.last_error = last_error_;
    state_out.mirror_state = mirror_state_name(state_);
    state_out.failures = failure_burst_;
    return true;
}

void SkinEngine::sync_mirror(const SkinRequest& request, bool allowed, SkinState& state_out) {
    // --- the state machine (spec §33) ----------------------------------------
    if (!request.target.hwnd) {
        // Nothing to mirror. If we were mirroring, the Premiere window is gone:
        // release everything and go back to waiting (spec §34).
        if (capture_.running() || mirror_.visible()) {
            teardown_mirror("Premiere closed");
        }
        set_state(MirrorState::WaitingForPremiere, "waiting for Premiere Pro");
        return;
    }
    if (!request.target.valid()) {
        // A window exists but is minimized, hidden or cloaked: suspend, keep state.
        if (mirror_.visible()) mirror_.hide();
        if (capture_.running() && (request.target.minimized || !request.target.visible)) {
            capture_.stop();
            attached_hwnd_ = nullptr;
        }
        set_state(request.target.minimized ? MirrorState::Suspended : MirrorState::WindowValidated,
                  request.target.minimized ? "Premiere is minimized" : "the Premiere window is not visible");
        return;
    }
    if (!allowed) {
        if (mirror_.visible()) mirror_.hide();
        if (capture_.running()) capture_.stop();
        if (request.suspend == SuspendReason::SkinDisabled || request.suspend == SuspendReason::OriginalTheme) {
            set_state(MirrorState::Suspended, suspend_reason_name(request.suspend));
        } else if (unsupported_) {
            set_state(MirrorState::Unsupported, "this host cannot mirror: see the log");
        } else {
            set_state(MirrorState::WindowValidated, "the style would change nothing");
        }
        state_out.mirror_active = false;
        state_out.mirror_capturing = false;
        state_out.mirror_note = note_;
        return;
    }

    if (state_ == MirrorState::WaitingForPremiere || state_ == MirrorState::PremiereFound) {
        set_state(MirrorState::PremiereFound, "Premiere Pro is running");
    }

    // The window is validated before anything is attached to it: the handle must
    // still belong to the process the detector found (Windows recycles handles).
    if (!window_belongs_to(request.target.hwnd, request.target.pid)) {
        set_state(MirrorState::PremiereFound, "the window no longer belongs to that process; reconnecting");
        if (capture_.running()) capture_.stop();
        attached_hwnd_ = nullptr;
        return;
    }
    if (state_ == MirrorState::PremiereFound) {
        set_state(MirrorState::WindowValidated, "main window validated");
    }
    // Remembered for the z-order re-assertion below: the mirror is placed above *this*
    // window, and `reassert_stacking()` needs it to notice that something raised
    // Premiere over it (which is what activating Premiere does to every ordinary
    // window). It is cleared whenever the mirror is torn down.
    target_ = request.target.hwnd;

    // --- the frame the mirror draws -------------------------------------------
    const MirrorRect overlay = overlay_for(request.target);
    if (overlay.empty()) {
        mirror_.hide();
        set_state(MirrorState::WindowValidated, "nothing visible to cover");
        return;
    }

    MirrorFrame frame;
    frame.anchor = request.target.hwnd;
    frame.insert_after = z_order_anchor(request.target.hwnd);
    frame.overlay = overlay;
    frame.captured = request.target.frame;
    frame.client_origin = client_origin_;
    frame.dpi = request.target.dpi;
    frame.panels = panels_;
    frame.size_agrees = true;
    const bool frame_moved = mirror_.set_frame(frame);
    if (place_again_) {
        // Asked for explicitly (a manual visibility check, a settings change that only
        // affects the material): place the window again with the frame it already has.
        place_again_ = false;
        if (mirror_.visible()) mirror_.reassert_placement();
    }
    if (frame_moved) {
        // A move or a resize is exactly when the mirror must not lag behind, so the
        // capture and the renderer are both asked for a short burst.
        capture_.request_burst(0.6);
        mirror_.request_burst(0.6);
    }

    // --- the capture ----------------------------------------------------------
    if (!capture_.running()) {
        const unsigned long long now_ms = GetTickCount64();
        if (now_ms < next_attempt_ms_) {
            state_out.mirror_note = "waiting to retry the capture";
            return;
        }
        // A window that changed identity (a new document, a new window) restarts it.
        const bool wrong_window = attached_hwnd_ != nullptr && attached_hwnd_ != request.target.hwnd;
        const bool restart = wrong_window || capture_.item_closed();
        if (restart || !capture_attempted_) {
            if (restart) capture_.stop();
            capture_attempted_ = true;
            set_state(MirrorState::CaptureInitializing, "creating the window capture");
            std::string capture_error;
            if (!start_capture(request, &capture_error)) {
                last_error_ = capture_error;
                state_out.mirror_note = capture_error;
                return;
            }
            log_info("mirror: capture started for 0x%p (pid %lu)", (void*)request.target.hwnd, request.target.pid);
        } else {
            return;
        }
    }

    // --- show the mirror once it has something to show ------------------------
    const CaptureStatus status = capture_.status();
    if (!frames_seen_ && status.frames > 0) frames_seen_ = true;
    if (state_ == MirrorState::CaptureInitializing && status.frames > 0) {
        set_state(MirrorState::CaptureActive, "frames are arriving");
    }
    if (!frames_seen_) {
        // Never show an empty window where Premiere's UI should be: the mirror appears
        // one capture later, which is a few milliseconds.
        if (mirror_.visible()) mirror_.hide();
        state_out.mirror_note = "waiting for the first captured frame";
        return;
    }

    if (!mirror_.visible()) {
        if (!mirror_.show()) {
            state_out.mirror_note = mirror_.stats().note;
            last_error_ = mirror_.stats().note;
            return;
        }
        log_info("mirror: on screen (%d,%d %dx%d) at %u dpi", overlay.x, overlay.y, overlay.width, overlay.height,
                 request.target.dpi);
    }
    set_state(MirrorState::MirrorActive, "mirroring Premiere");
    state_out.mirror_active = true;
    state_out.mirror_capturing = true;
    state_out.mirror_note = note_;
}

void SkinEngine::reassert_stacking() {
    if (!mirror_.visible() || target_ == nullptr) return;
    if (!IsWindow(target_)) return;
    if (window_is_above(mirror_.window(), target_)) return;
    log_debug("mirror: re-asserting the stacking order above 0x%p", (void*)target_);
    // One SetWindowPos with the placement the frame already carries. This has to be a
    // placement and not a flag for the next apply: activating Premiere raises it above
    // every ordinary window, and the geometry it was placed with has not changed, so
    // an unchanged frame is exactly the case that would never be re-placed.
    if (!mirror_.reassert_placement()) {
        log_warn("mirror: the stacking order could not be re-asserted (%s)", mirror_.stats().note.c_str());
    }
}

void SkinEngine::recover_device(const char* why) {
    // A removed device cannot draw again: every resource built on it is gone, so the
    // renderer is destroyed and rebuilt from scratch. The capture holds the same device
    // and is stopped first (it must not keep a reference to it across the rebuild).
    capture_.stop();
    attached_hwnd_ = nullptr;
    attached_pid_ = 0;
    frames_seen_ = false;
    capture_attempted_ = false;
    mirror_.destroy();

    std::string error;
    if (!mirror_.create(GetModuleHandleW(nullptr), &error)) {
        last_error_ = error;
        note_ = error;
        ++failure_burst_;
        if (failure_burst_ >= kMaxFailureBurst) {
            unsupported_ = true;
            set_state(MirrorState::Unsupported, "the GPU device was lost and could not be rebuilt");
        } else {
            next_attempt_ms_ = GetTickCount64() + kRetryBackoffMs * failure_burst_;
            set_state(MirrorState::CaptureFailed, "the GPU device was lost; retrying");
        }
        log_error("mirror: could not rebuild after the device was lost: %s", error.c_str());
        return;
    }
    mirror_.attach_capture(&capture_);
    mirror_.set_animations(style_.animations);
    failure_burst_ = 0;
    next_attempt_ms_ = 0;
    log_info("mirror: rebuilt after a device loss (%s)", why == nullptr ? "device lost" : why);
    set_state(MirrorState::CaptureInitializing, "the GPU device was replaced");
}

bool SkinEngine::probe_on_screen(std::string* detail) {
    const MirrorRenderer::Stats& stats = mirror_.stats();
    const CaptureStatus capture = capture_.status();
    const bool visible = mirror_.visible();
    const bool any_visible = visible;

    std::string text = visible ? "the skin is on screen" : "the skin is NOT on screen";
    text += "\n";
    if (visible) {
        text += "  mirror: on screen, " + std::to_string(stats.presents) + " presents, " +
                std::to_string(capture.frames) + " captured frames, " + std::to_string(capture.content_width) + "x" +
                std::to_string(capture.content_height) + ", " + std::to_string(stats.paced_fps) + " fps";
        text += " (structural, not a pixel check: the mirror is painted by the compositor)";
    } else {
        text += "  mirror: not showing (" + note_ + ")";
    }
    text += "\n";
    text += "  lifecycle: " + std::string(mirror_state_name(state_));
    if (detail != nullptr) *detail = text;

    if (!visible) log_warn("visibility check: %s", note_.c_str());
    return any_visible;
}

}  // namespace win
}  // namespace azy
