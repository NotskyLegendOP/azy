// Azy Skin — Win32 layer: SkinEngine (the composition manager).
//
// One place decides what the skin is doing right now. The rebuild replaced the old
// per-layer bookkeeping (DWM attributes, a ring of layered strips, a veil, and a
// pile of booleans describing them) with a single lifecycle state machine and one
// visual layer: the mirror of the real Premiere window.
//
//   WAITING_FOR_PREMIERE -> PREMIERE_FOUND -> WINDOW_VALIDATED
//      -> CAPTURE_INITIALIZING -> CAPTURE_ACTIVE -> MIRROR_ACTIVE
//
// Every failure moves to a recovery state (RETRY_WAIT, UNSUPPORTED) and never takes
// the process down with it (spec §33, §34, §40).
#pragma once

#include <string>

#include "azy/core/mirror_style.hpp"
#include "azy/core/panel_map.hpp"
#include "azy/win32/capture/window_capture.hpp"
#include "azy/win32/mirror/mirror_renderer.hpp"
#include "azy/win32/skin/skin_types.hpp"

namespace azy {
namespace win {

// The lifecycle the spec asks for, in one enum. It is reported verbatim in the
// diagnostics and drives the transitions; nothing else in the engine keeps state.
enum class MirrorState {
    WaitingForPremiere,  // nothing to mirror
    PremiereFound,       // a process is there, no usable window yet
    WindowValidated,     // a window that belongs to that process, validated
    CaptureInitializing, // the capture is being created
    CaptureActive,       // frames are arriving
    MirrorActive,        // the skinned mirror is on screen
    CaptureFailed,       // the last attempt failed; a retry is scheduled
    Unsupported,         // this host cannot do it at all; do not keep trying
    Suspended,           // the user or the window state asked the skin to stand down
};

const char* mirror_state_name(MirrorState state);

class SkinEngine {
public:
    ~SkinEngine() { shutdown(); }

    bool initialize(std::string* error);
    void shutdown();

    // Applies (or removes) the skin for this request. Returns true when the on-screen
    // result actually changed.
    bool apply(const SkinRequest& request, SkinState& state_out);

    // Full teardown: stops the capture, destroys the mirror and every GPU resource.
    void revert();

    bool surface_visible() const { return mirror_.visible(); }

    // Asks for the window to be placed (and the next frame presented) again even though
    // nothing about it changed: what "Check visibility" needs in order to have something
    // to look at, and what a change that only affects the material uses.
    void invalidate() { place_again_ = true; }

    // Puts the mirror back in front of Premiere when something raised Premiere above
    // it. A z-order walk, and only when the order is actually wrong one SetWindowPos.
    void reassert_stacking();

    // Checks with the desktop that the mirror is really reaching the screen. The
    // mirror has no redirection bitmap (the compositor paints it), so a pixel read is
    // not a trustworthy answer about it: what is reported is everything the process
    // itself knows, and the text says plainly that it is structural.
    bool probe_on_screen(std::string* detail);

    // Clears a previous failure so the next apply() tries again (the manual retry
    // behind "Check visibility").
    void retry_overlay();

    MirrorState state() const { return state_; }
    // The lifecycle state's name, exactly as the log writes it (spec §38 reports it
    // verbatim, so the debug screen and the log cannot disagree).
    const char* state_name() const { return mirror_state_name(state_); }
    const MirrorRenderer::Stats& mirror_stats() const { return mirror_.stats(); }
    CaptureStatus capture_status() const { return capture_.status(); }
    HWND mirror_window() const { return mirror_.window(); }
    bool mirror_supported() const { return !unsupported_; }
    const std::string& status_note() const { return note_; }

    const std::vector<PanelRect>& panel_map() const { return panels_; }
    Rect client_origin() const { return client_origin_; }
    const std::string& last_error() const { return last_error_; }

private:
    void sync_mirror(const SkinRequest& request, bool allowed, SkinState& state_out);
    void teardown_mirror(const char* reason);
    void set_state(MirrorState state, const char* why);
    bool start_capture(const SkinRequest& request, std::string* error);
    // The GPU device was lost (driver update, TDR, hybrid-GPU switch): rebuild the
    // renderer, bounded, so a machine that cannot hold a device ends up in the honest
    // Unsupported state instead of rebuilding in a loop.
    void recover_device(const char* why);
    static MirrorRect overlay_for(const SkinTarget& target);

    MirrorRenderer mirror_;
    WindowCapture capture_;
    MirrorStyle style_;

    MirrorState state_ = MirrorState::WaitingForPremiere;
    bool unsupported_ = false;
    bool capture_attempted_ = false;
    bool frames_seen_ = false;
    unsigned failure_burst_ = 0;
    unsigned long long next_attempt_ms_ = 0;
    unsigned long long attached_pid_ = 0;
    HWND attached_hwnd_ = nullptr;
    std::string note_ = "waiting for Premiere Pro";
    std::string last_error_;

    std::vector<PanelRect> panels_;
    Rect client_origin_;
    HWND target_ = nullptr;
    bool place_again_ = false;
};

}  // namespace win
}  // namespace azy
