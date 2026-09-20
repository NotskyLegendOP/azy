#include "azy/win32/skin/skin_engine.hpp"
#include <algorithm>
#include <string>

#include "azy/core/log.hpp"
#include "azy/core/ring_layout.hpp"
#include "azy/core/version_string.hpp"
#include "azy/core/strings.hpp"
#include "azy/win32/os/win_util.hpp"
#include "azy/win32/skin/input_guard.hpp"

namespace azy {
namespace win {
namespace {

// Where the capture starts before the overlay's own pacing controller takes over
// (it raises this while frames keep arriving and lowers it when they stop).
constexpr unsigned kOverlayStartFps = 12;

bool same_color(const Rgba& a, const Rgba& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// The corner radius used for Azy's own ring.
//
// When Azy asked DWM to round the window, the ring has to use the same radius or
// the window's rounded corner would show outside it. Windows rounds frames with
// an 8 DIP radius, so that is what we mirror. When the frame is square (older
// Windows, maximised window, rounding disabled) the user's radius applies and
// the ring simply sits inside a square corner.
int ring_radius_px(const SkinRequest& request, bool frame_rounded) {
    if (frame_rounded) return dip_to_px(8, request.target.dpi);
    const int dip = request.target.effective_radius_dip(request.palette.corner_radius_dip);
    return dip_to_px(dip, request.target.dpi);
}

// Smallest window Azy's four-strip ring is meaningful on. Below this the ring
// would be almost entirely window (typical for the tiny helper windows Premiere
// creates internally), so the surface is skipped - silently, because it is not a
// failure, just a window that is too small to decorate.
bool window_large_enough_for_ring(const Rect& frame, int band_px, int radius_px) {
    // Shares the surface's geometry helper, so the engine and the surface can never
    // disagree about which windows get a ring.
    return ring_geometry(frame, band_px, radius_px).valid;
}

// Band thickness of the composition surface, in physical pixels: how far the
// edge treatment reaches into the window. Bounded so a small floating window
// cannot be swallowed by its own decoration, and kept thin in performance mode,
// where the treatment is a static edge rather than a shaded one.
int ring_band_px(const SkinRequest& request) {
    // Performance mode: a bare edge (two constant strokes).
    if (request.performance_mode) {
        const int thin = dip_to_px(3, request.target.dpi);
        const int shorter_px = request.target.visible_frame.width() < request.target.visible_frame.height()
                                   ? request.target.visible_frame.width()
                                   : request.target.visible_frame.height();
        const int cap = shorter_px / 4;
        const int band = thin < cap ? thin : cap;
        return band < 2 ? 2 : band;
    }

    // With the whole-window overlay on, the edge treatment deepens with it: a 10px
    // band plus a tint reads as "a border and a wash", while a wide soft falloff
    // reads as one skin. The extra width follows the overlay's own strength, so the
    // single slider controls both, and switching the overlay off restores exactly
    // the previous edge.
    const double veil = static_cast<double>(request.palette.surface_veil.a) / 255.0;
    const double extra_dip = veil > 0.0 ? 8.0 + 90.0 * veil : 0.0;
    const int nominal = dip_to_px(10.0 + extra_dip, request.target.dpi);
    const int shorter = request.target.visible_frame.width() < request.target.visible_frame.height()
                            ? request.target.visible_frame.width()
                            : request.target.visible_frame.height();
    const int cap = shorter / 4;
    const int band = nominal < cap ? nominal : cap;
    return band < 2 ? 2 : band;
}

}  // namespace

bool SkinEngine::VisualKey::operator==(const VisualKey& other) const {
    return hwnd == other.hwnd && dark_frame == other.dark_frame && frame_colors == other.frame_colors &&
           rounded_frame == other.rounded_frame && frame_backdrop == other.frame_backdrop &&
           same_color(frame_caption, other.frame_caption) && same_color(frame_border, other.frame_border) &&
           same_color(frame_text, other.frame_text) && surface == other.surface &&
           surface_rect == other.surface_rect && dpi == other.dpi && radius_px == other.radius_px &&
           band_px == other.band_px && shadow == other.shadow && glass == other.glass &&
           same_color(bezel, other.bezel) && same_color(fill, other.fill) &&
           same_color(border, other.border) && same_color(highlight, other.highlight) &&
           same_color(shadow_color, other.shadow_color);
}

bool SkinEngine::initialize(std::string* error) {
    // The surface window is created lazily on the first successful apply, so Azy
    // stays completely invisible (no window at all) until it has something to
    // draw on.
    (void)error;
    return true;
}

void SkinEngine::shutdown() {
    revert();
    // Azy is going away: give the strips, their DIB sections and their device
    // contexts back to Windows instead of leaving hidden layered windows behind.
    surface_.destroy();
    veil_.destroy();
    debug_.destroy();
    teardown_overlay();
    panels_.clear();
    target_ = nullptr;
}

void SkinEngine::revert() {
    composer_.revert();
    surface_.hide();
    veil_.hide();
    debug_.hide();
    gloss_.hide();
    capture_.stop();
    duplicate_active_ = false;
    overlay_report_.active = false;
    overlay_report_.capturing = false;
    has_key_ = false;
    last_key_ = VisualKey{};
}

void SkinEngine::teardown_overlay() {
    capture_.stop();
    gloss_.destroy();
    overlay_created_ = false;
    duplicate_active_ = false;
    overlay_attached_ = nullptr;
    overlay_attached_pid_ = 0;
    overlay_report_.created = false;
    overlay_report_.active = false;
    overlay_report_.capturing = false;
    overlay_report_.note = "not running";
}

void SkinEngine::retry_overlay() {
    // A manual retry is a fresh start: the failure burst, the backoff and the
    // "this host cannot do it" verdict are all cleared, because the user may have
    // changed something (installed a driver, started Premiere with a different
    // window) since the last attempt.
    overlay_disabled_ = false;
    overlay_failure_burst_ = 0;
    overlay_next_attempt_ms_ = 0;
    overlay_report_.supported = true;
    overlay_report_.note = "retrying";
}

void SkinEngine::release_surface() {
    surface_.destroy();
    veil_.destroy();
    debug_.destroy();
    // Premiere is gone: the duplicate and its capture go with it, so Azy holds no
    // GPU memory and no worker thread while there is nothing to mirror.
    teardown_overlay();
    panels_.clear();
    target_ = nullptr;
    log_debug("composition surface destroyed");
}

// The duplicate window: create it, keep the capture attached, place it, show it.
//
// Everything here is written so that a failure ends with the *other* layers still
// doing their job: the duplicate is never allowed to be the single point of
// failure for the skin.
void SkinEngine::sync_overlay(const SkinRequest& request, bool allowed, bool want_capture, SkinState& state_out) {
    // Every path below overwrites these; the reset makes sure a path that returns
    // early cannot leave last tick's verdict in place.
    state_out.duplicate_active = false;
    state_out.duplicate_capturing = capture_.running();
    overlay_style_ = make_overlay_style(request.palette, request.appearance, request.performance_mode,
                                        request.features.edge_surface_rounded);
    gloss_.set_style(overlay_style_);
    gloss_.set_performance_mode(request.performance_mode);

    const bool style_wanted = gloss_.style_visible() && !overlay_style_is_passthrough(overlay_style_);
    if (!allowed || !style_wanted || overlay_disabled_) {
        gloss_.hide();
        capture_.stop();
        duplicate_active_ = false;
        overlay_attached_ = nullptr;
        overlay_attached_pid_ = 0;
        overlay_report_.active = false;
        overlay_report_.capturing = false;
        if (overlay_disabled_) {
            overlay_report_.note = "not available on this host";
        } else if (!allowed) {
            overlay_report_.note = suspend_reason_name(request.suspend);
        } else {
            overlay_report_.note = "the style would not change anything";
        }
        state_out.duplicate_active = false;
        state_out.duplicate_capturing = false;
        state_out.duplicate_note = overlay_report_.note;
        return;
    }

    const unsigned long long now = GetTickCount64();

    // A lost GPU device is not a failure of the capture: it is the whole pipeline
    // going away at once. Everything is released and rebuilt from scratch, with the
    // backoff cleared so the rebuild happens on this very apply.
    if (overlay_created_ && gloss_.needs_recreate()) {
        log_warn("overlay: the GPU device was lost - rebuilding the duplicate window");
        capture_.stop();
        gloss_.destroy();
        overlay_created_ = false;
        overlay_attached_ = nullptr;
        overlay_attached_pid_ = 0;
        overlay_failure_burst_ = 0;
        overlay_next_attempt_ms_ = 0;
        overlay_report_.created = false;
        overlay_report_.note = "rebuilding after a lost GPU device";
    }

    if (!overlay_created_) {
        if (now < overlay_next_attempt_ms_) {
            overlay_report_.note = "waiting before the next attempt";
            state_out.duplicate_note = overlay_report_.note;
            return;
        }
        std::string error;
        ++overlay_attempts_;
        if (!gloss_.create(GetModuleHandleW(nullptr), &error)) {
            overlay_failure_burst_ += 1;
            // Three failed attempts in a session is a verdict about this host, not
            // a hiccup: the duplicate is switched off and the ring and the veil
            // keep the skin. The user can ask for a retry from the settings window.
            if (overlay_failure_burst_ >= 3) {
                overlay_disabled_ = true;
                overlay_report_.supported = false;
                log_warn("overlay: giving up after %d attempts - %s", overlay_failure_burst_, error.c_str());
            } else {
                log_warn("overlay: could not be created - %s", error.c_str());
            }
            overlay_next_attempt_ms_ = now + 4000ull * static_cast<unsigned long long>(overlay_failure_burst_);
            overlay_report_.note = error;
            state_out.last_error = error;
            state_out.duplicate_note = overlay_report_.note;
            return;
        }
        overlay_created_ = true;
        overlay_report_.created = true;
        log_info("overlay: duplicate window created (attempt %llu)", overlay_attempts_);
    }

    // --- the capture ---------------------------------------------------------
    CaptureStatus capture_status = capture_.status();
    if (!want_capture) {
        capture_.stop();
        gloss_.hide();
        duplicate_active_ = false;
        overlay_attached_ = nullptr;
        overlay_attached_pid_ = 0;
        overlay_report_.active = false;
        overlay_report_.capturing = false;
        overlay_report_.note = suspend_reason_name(request.suspend);
        state_out.duplicate_active = false;
        state_out.duplicate_capturing = false;
        state_out.duplicate_note = overlay_report_.note;
        return;
    }

    const bool wrong_window = overlay_attached_ != nullptr &&
                              (overlay_attached_ != request.target.hwnd || overlay_attached_pid_ != request.target.pid);
    if (capture_.running() && (wrong_window || capture_.item_closed())) {
        // The window the capture was attached to is gone (Premiere restarted, or
        // the handle was recycled): drop it and let the block below start over.
        capture_.stop();
        overlay_attached_ = nullptr;
        overlay_attached_pid_ = 0;
        overlay_report_.capturing = false;
    }

    if (!capture_.running()) {
        if (now < overlay_next_attempt_ms_) {
            gloss_.hide();
            duplicate_active_ = false;
            overlay_report_.active = false;
            overlay_report_.capturing = false;
            overlay_report_.note = "waiting before the next capture attempt";
            state_out.duplicate_note = overlay_report_.note;
            return;
        }
        if (gloss_.shared_device() == nullptr) {
            overlay_report_.note = "no GPU device";
            state_out.duplicate_note = overlay_report_.note;
            return;
        }
        std::string detail;
        const CaptureStart result = capture_.start(request.target.hwnd, request.target.pid,
                                                   gloss_.shared_device(),
                                                   kOverlayStartFps, &detail);
        if (result == CaptureStart::Started) {
            overlay_attached_ = request.target.hwnd;
            overlay_attached_pid_ = request.target.pid;
            overlay_failure_burst_ = 0;
            overlay_next_attempt_ms_ = 0;
            overlay_report_.capturing = true;
            overlay_report_.note = "capturing";
            log_info("overlay: mirroring the Premiere window (pid %lu)", request.target.pid);
        } else if (result == CaptureStart::Unsupported) {
            // This is a fact about the machine (an older Windows, or a locked-down
            // build), not a defect: no retry storm, no failure counter, no Safe
            // Mode. The note explains it and the other layers keep working.
            overlay_disabled_ = true;
            overlay_report_.supported = false;
            overlay_report_.note = detail;
            log_warn("overlay: this host cannot mirror a window - %s", detail.c_str());
            state_out.duplicate_note = overlay_report_.note;
            return;
        } else if (result == CaptureStart::Minimized) {
            // Expected while Premiere is minimised; nothing to report.
            overlay_report_.note = detail;
            state_out.duplicate_note = overlay_report_.note;
            return;
        } else {
            overlay_failure_burst_ += 1;
            overlay_next_attempt_ms_ = now + 4000ull * static_cast<unsigned long long>(overlay_failure_burst_);
            overlay_report_.capturing = false;
            overlay_report_.note = detail;
            state_out.last_error = detail;
            log_warn("overlay: capture did not start (%s) - %s", capture_start_text(result), detail.c_str());
            state_out.duplicate_note = overlay_report_.note;
            return;
        }
    }

    capture_status = capture_.status();
    overlay_report_.capturing = capture_status.running;

    // The visible size of the capture and the rectangle Windows reports for the
    // window are two different numbers on a DPI-scaled host. When they disagree,
    // the mirror would be scaled wrongly; the debug overlay says so instead of
    // pretending, and the capture is restarted on the sizes we know still match.
    int content_width = 0;
    int content_height = 0;
    capture_.source_size(&content_width, &content_height);
    const bool size_agrees = content_width <= 0 || content_height <= 0 ||
                             (std::abs(content_width - request.target.frame.width()) <= 2 &&
                              std::abs(content_height - request.target.frame.height()) <= 2);

    // --- geometry and presentation -------------------------------------------
    // Nothing goes on screen until the capture has produced a frame: an empty
    // composition window would be a hole where Premiere's UI should be, which is
    // exactly the complaint the duplicate window exists to answer.
    if (capture_status.frames == 0) {
        gloss_.hide();
        duplicate_active_ = false;
        overlay_report_.active = false;
        overlay_report_.note = "waiting for the first captured frame";
        state_out.duplicate_active = false;
        state_out.duplicate_capturing = true;
        state_out.duplicate_note = overlay_report_.note;
        return;
    }

    GlossFrame frame;
    frame.overlay = overlay_rect(request.target.visible_frame, request.target.monitor, request.target.work_area,
                                 request.target.maximized, request.target.fullscreen);
    frame.captured = request.target.frame;
    frame.client_origin = client_origin_;
    frame.dpi = request.target.dpi;
    frame.panels = panels_;
    frame.anchor = request.target.hwnd;
    frame.insert_after = z_order_anchor(request.target.hwnd);
    frame.size_agrees = size_agrees;

    if (frame.overlay.empty()) {
        gloss_.hide();
        duplicate_active_ = false;
        overlay_report_.active = false;
        overlay_report_.note = "nothing visible to cover";
        state_out.duplicate_active = false;
        state_out.duplicate_note = overlay_report_.note;
        return;
    }

    if (!gloss_.set_frame(frame)) {
        gloss_.hide();
        duplicate_active_ = false;
        overlay_report_.active = false;
        overlay_report_.note = gloss_.stats().note;
        state_out.duplicate_active = false;
        state_out.duplicate_note = overlay_report_.note;
        return;
    }

    if (gloss_.show()) {
        duplicate_active_ = true;
        overlay_report_.active = true;
        overlay_report_.note = "mirroring";
        state_out.duplicate_active = true;
        state_out.duplicate_capturing = true;
        state_out.duplicate_note = overlay_report_.note;
        log_debug("overlay: %dx%d over the window, capture %dx%d, uv %.3f,%.3f", frame.overlay.width(),
                  frame.overlay.height(), content_width, content_height, gloss_.stats().uv[0], gloss_.stats().uv[1]);
    } else {
        duplicate_active_ = false;
        overlay_report_.active = false;
        overlay_report_.note = gloss_.stats().note;
        state_out.duplicate_active = false;
        state_out.duplicate_note = overlay_report_.note;
    }
}

SkinEngine::VisualKey SkinEngine::build_key(const SkinRequest& request) const {
    VisualKey key;
    key.hwnd = request.target.hwnd;

    const bool frame_rounded = request.features.rounded_frame && !request.target.maximized &&
                               !request.target.fullscreen &&
                               request.target.effective_radius_dip(request.palette.corner_radius_dip) > 0;

    key.dark_frame = request.features.dark_frame;
    key.frame_colors = request.features.frame_colors && request.palette.apply_frame_colors;
    key.rounded_frame = frame_rounded;
    key.frame_backdrop = request.features.frame_backdrop;
    key.frame_caption = request.palette.frame_caption;
    key.frame_border = request.palette.frame_border;
    key.frame_text = request.palette.frame_text;

    key.surface = request.features.edge_surface;
    key.surface_rect = request.target.visible_frame;
    key.dpi = request.target.dpi;
    // FeatureSet::edge_surface_rounded off (performance mode, disabled in the
    // INI, or a rounded-corner-less host) means a square ring.
    key.radius_px = request.features.edge_surface_rounded ? ring_radius_px(request, frame_rounded) : 0;
    key.band_px = ring_band_px(request);
    key.shadow = request.palette.shadow_enabled;
    key.glass = request.palette.surface_fill.a < 255;
    key.bezel = request.palette.surface_bezel;
    key.fill = request.palette.surface_fill;
    key.border = request.palette.surface_border;
    key.highlight = request.palette.surface_highlight;
    key.shadow_color = request.palette.surface_shadow;
    if (request.target.maximized || request.target.fullscreen) {
        // On a screen-filling window the ring runs along the physical screen
        // edges, where a full-strength bezel reads as a border drawn around the
        // display. Halve it and leave a soft edge treatment instead.
        key.bezel.a = static_cast<unsigned char>(key.bezel.a / 2);
    }
    if (request.performance_mode) {
        // Performance mode: static colours only. The translucent wash is dropped
        // (the bezel and the hairline carry the look) and so is the shadow
        // gradient, leaving two constant strokes per strip.
        key.fill.a = 0;
        key.glass = false;
        key.shadow = false;
        key.shadow_color.a = 0;
    }
    return key;
}

bool SkinEngine::apply(const SkinRequest& request, SkinState& state_out) {
    state_out.suspend = request.suspend;

    const bool want_skin = request.skin_enabled && request.palette.visible;
    const bool window_alive = request.target.hwnd != nullptr && IsWindow(request.target.hwnd);
    target_ = window_alive ? request.target.hwnd : nullptr;

    // The frame keeps its DWM treatment while the window merely exists: those
    // attributes cost nothing while Premiere is minimised or hidden, and
    // re-applying them on restore would flash the original title bar. Only a
    // disabled skin (or a gone window) removes them.
    const bool keep_frame = want_skin && window_alive;

    if (!keep_frame) {
        const bool had_frame = composer_.is_applied();
        const bool had_surface = surface_.visible();
        const bool had_duplicate = duplicate_active_ || capture_.running() || overlay_created_;
        if (had_frame) {
            composer_.revert();
            state_out.frame_applied = false;
        }
        if (had_surface) {
            surface_.hide();
            state_out.surface_visible = false;
        }
        has_key_ = false;
        // The duplicate goes with the window: its capture is stopped and its GPU
        // resources are released, so a stopped Azy holds no GPU memory and no
        // worker thread.
        if (had_duplicate) {
            gloss_.hide();
            capture_.stop();
            duplicate_active_ = false;
            overlay_attached_ = nullptr;
            overlay_attached_pid_ = 0;
            overlay_report_.active = false;
            overlay_report_.capturing = false;
            overlay_report_.note = suspend_reason_name(request.suspend);
            state_out.duplicate_note = overlay_report_.note;
        }
        // Nothing is attached, so the map describes a window that is no longer
        // tracked: leaving it in place would hand the region work rectangles for a
        // window that is gone.
        panels_.clear();
        if (had_frame || had_surface || had_duplicate) {
            log_info("skin removed (%s)", suspend_reason_name(request.suspend));
        }
        return had_frame || had_surface || had_duplicate;
    }

    // Is the duplicate window the thing the user is looking at? While it is, the
    // ring and the veil stand down: both are placed under it, so drawing them
    // would spend GDI work and GPU memory on pixels nobody can see. This is the
    // "avoid double rendering" rule of the overlay design, and it is also what
    // keeps the skin's cost from doubling when the overlay is on.
    const bool duplicate_now = duplicate_active_ ||
                               (overlay_created_ && !overlay_disabled_ && capture_.running() &&
                                capture_.status().frames > 0);

    const VisualKey key = build_key(request);
    const bool frame_changed = !has_key_ || last_key_.hwnd != key.hwnd || last_key_.dark_frame != key.dark_frame ||
                               last_key_.frame_colors != key.frame_colors ||
                               last_key_.rounded_frame != key.rounded_frame ||
                               last_key_.frame_backdrop != key.frame_backdrop ||
                               !same_color(last_key_.frame_caption, key.frame_caption) ||
                               !same_color(last_key_.frame_border, key.frame_border) ||
                               !same_color(last_key_.frame_text, key.frame_text);

    // The composition surface is only shown while the window is genuinely on
    // screen and the skin is not suspended for any reason.
    const bool surface_requested = key.surface && request.suspend == SuspendReason::None && request.target.valid() &&
                                   !duplicate_now;
    const bool surface_changed = !has_key_ || last_key_.hwnd != key.hwnd || last_key_.surface != key.surface ||
                                 last_key_.surface_rect != key.surface_rect || last_key_.dpi != key.dpi ||
                                 last_key_.radius_px != key.radius_px || last_key_.band_px != key.band_px ||
                                 last_key_.shadow != key.shadow || last_key_.glass != key.glass ||
                                 !same_color(last_key_.bezel, key.bezel) ||
                                 !same_color(last_key_.fill, key.fill) ||
                                 !same_color(last_key_.border, key.border) ||
                                 !same_color(last_key_.highlight, key.highlight) ||
                                 !same_color(last_key_.shadow_color, key.shadow_color);

    bool changed = false;

    if (frame_changed) {
        const DwmComposer::ApplyResult result =
            composer_.apply(request.target, request.palette, request.features,
                            request.target.maximized || request.target.fullscreen);
        state_out.frame_applied = composer_.is_applied();
        if (result.any_failed) {
            ++state_out.failures;
            state_out.last_error = result.error;
            log_debug("dwm apply partially rejected: %s", result.error.c_str());
        }
        changed = true;
    }

    // The overlay covers the whole window; the ring decorates its edge. Both are
    // tracked, and the ring is always presented first because the overlay is then
    // stacked *under* it (see below), which keeps the 1px hairline crisp.
    const bool overlay_requested = key.overlay && request.suspend == SuspendReason::None && request.target.valid() &&
                                   !duplicate_now;
    const bool overlay_changed = !has_key_ || last_key_.hwnd != key.hwnd || last_key_.overlay != key.overlay ||
                                 !same_color(last_key_.veil, key.veil) ||
                                 last_key_.surface_rect != key.surface_rect;

    // A window too small for a ring is still worth covering with the overlay.
    const bool ring_fits =
        surface_requested && window_large_enough_for_ring(key.surface_rect, key.band_px, key.radius_px);

    if (surface_requested && !ring_fits) {
        if (surface_.visible()) surface_.hide();
        state_out.surface_visible = false;
    } else if (surface_requested && surface_changed) {
        RingVisual visual;
        // The ring is described in frame coordinates; CompositionSurface splits it
        // into four thin strips and hands each one the right origin, so a 1px line
        // stays exactly one pixel wide at every DPI without any resampling.
        visual.width_px = key.surface_rect.width();
        visual.height_px = key.surface_rect.height();
        visual.band_px = key.band_px;
        visual.radius_px = key.radius_px;
        visual.draw_fill = key.fill.a > 0;
        visual.draw_shadow = key.shadow;
        visual.draw_highlight = request.features.edge_surface_rounded;
        visual.draw_border = true;
        visual.bezel = key.bezel;
        visual.fill = key.fill;
        visual.border = key.border;
        visual.highlight = key.highlight;
        visual.shadow = key.shadow_color;

        std::string error;
        if (surface_.present(request.target.hwnd, key.surface_rect, visual, &error)) {
            state_out.surface_visible = true;
            changed = true;
        } else {
            ++state_out.failures;
            state_out.last_error = error;
            log_warn("composition surface failed: %s", error.c_str());
            state_out.surface_visible = false;
        }
    } else if (!surface_requested && surface_.visible()) {
        surface_.hide();
        state_out.surface_visible = false;
        changed = true;
    } else {
        state_out.surface_visible = surface_.visible();
    }

    // --- the overlay ------------------------------------------------------
    // One constant-alpha window over the whole tracked window. A failed overlay is
    // logged once and retried on the next apply (it is only recorded as done when
    // it is really on screen).
    if (overlay_requested && (overlay_changed || state_out.surface_visible)) {
        std::string veil_error;
        if (veil_.present(request.target.hwnd, state_out.surface_visible ? surface_.hwnd() : nullptr,
                          key.surface_rect, key.veil, &veil_error)) {
            state_out.overlay_visible = true;
            changed = true;
        } else {
            ++state_out.failures;
            state_out.last_error = veil_error;
            log_warn("overlay veil failed: %s", veil_error.c_str());
            state_out.overlay_visible = veil_.visible();
        }
    } else if (!overlay_requested && veil_.visible()) {
        veil_.hide();
        state_out.overlay_visible = false;
        changed = true;
    } else {
        state_out.overlay_visible = veil_.visible();
    }

    // --- the panel map (spec §3 Layer 2, §41) --------------------------------
    // Built from the same geometry the ring uses, so it is exact by construction:
    // a ratio layout applied to the client rectangle at this DPI. A mistake here
    // costs a missing region, never a broken layout, which is why a panel that
    // does not fit is marked unusable instead of being driven to a negative size.
    // The model describes the *client* area, not the frame: Premiere's panels are
    // inside the client area, and using the frame would put the menu bar band over
    // the title bar. Asking Windows for the real client rectangle keeps this exact
    // at every window state (the relationship between frame and client changes with
    // the caption, the border and the DPI).
    Rect client_screen;
    if (!window_client_rect(request.target.hwnd, client_screen)) {
        client_screen = key.surface_rect;
    }
    const Rect client = Rect::from_size(0, 0, client_screen.width(), client_screen.height());
    const std::vector<PanelRect> panels = build_panel_map(client, request.target.dpi, request.workspace);
    const bool map_changed =
        panels.size() != panels_.size() ||
        !std::equal(panels.begin(), panels.end(), panels_.begin(),
                    [](const PanelRect& a, const PanelRect& b) {
                        return a.id == b.id && a.usable == b.usable && a.rect == b.rect;
                    });
    if (map_changed) {
        panels_ = panels;
        client_origin_ = client_screen;
        // One line per layout change (never per timer tick): the model's own view of
        // the window, which is what a bug report needs to be actionable.
        log_info("panel map (%s workspace, %dx%d client, %u dpi):\n%s",
                 workspace_name(resolve_workspace(request.workspace)), client.width(), client.height(),
                 request.target.dpi, describe_panel_map(panels_).c_str());
    }

    // --- the duplicate window ------------------------------------------------
    // Last, because it consumes the panel map this apply() just built, and because
    // it is the layer that overrides the other two: while it is on screen they are
    // suppressed above, and here is where "on screen" is decided.
    {
        const bool duplicate_allowed = request.skin_enabled && request.palette.visible &&
                                       request.features.window_overlay && request.target.valid() &&
                                       (request.suspend == SuspendReason::None || request.suspend == SuspendReason::Moving ||
                                        request.suspend == SuspendReason::Dragging);
        const bool capture_wanted = duplicate_allowed && request.suspend != SuspendReason::FullscreenTransition;
        sync_overlay(request, duplicate_allowed, capture_wanted, state_out);
        if (state_out.duplicate_active && !duplicate_now) changed = true;
        if (!state_out.duplicate_active && duplicate_now) changed = true;
    }

    // --- debug overlay (spec §41) -------------------------------------------
    // The rectangles are translated into screen coordinates here: the map is built
    // for a client area that starts at (0,0), while the overlay window covers the
    // same rectangle the ring does.
    const bool want_debug = request.debug_mode && request.suspend == SuspendReason::None &&
                            request.target.valid() && request.skin_enabled;
    const bool debug_requested = want_debug && !panels_.empty();
    if (debug_requested) {
        std::vector<PanelRect> on_screen = panels_;
        for (PanelRect& panel : on_screen) {
            if (!panel.usable) continue;
            panel.rect.left += client_origin_.left;
            panel.rect.top += client_origin_.top;
            panel.rect.right += client_origin_.left;
            panel.rect.bottom += client_origin_.top;
        }
        std::vector<std::string> facts;
        facts.push_back("Azy Skin " + std::string(kAppVersion) + " - debug mode");
        facts.push_back(str_format("window 0x%p class '%s'  %dx%d at (%d,%d)", (void*)request.target.hwnd,
                                   to_utf8(request.target.window_class).c_str(),
                                   request.target.visible_frame.width(), request.target.visible_frame.height(),
                                   request.target.visible_frame.left, request.target.visible_frame.top));
        facts.push_back(str_format("client %dx%d at (%d,%d) | %u dpi (%d%%) | screen (%d,%d)-(%d,%d)",
                                   client.width(), client.height(), client_origin_.left, client_origin_.top,
                                   request.target.dpi, static_cast<int>(request.target.dpi * 100u / 96u),
                                   request.target.monitor.left, request.target.monitor.top,
                                   request.target.monitor.right, request.target.monitor.bottom));
        facts.push_back(str_format("workspace %s | %zu panels modelled, %zu usable",
                                   workspace_name(resolve_workspace(request.workspace)), panels_.size(),
                                   static_cast<size_t>(std::count_if(panels_.begin(), panels_.end(),
                                                                     [](const PanelRect& p) { return p.usable; }))));
        const GlossOverlay::Stats& ostats = gloss_.stats();
        const CaptureStatus cstatus = capture_.status();
        const OverlayReport& report = overlay_report();
        facts.push_back(str_format("duplicate 0x%p | ring 0x%p | anchor 0x%p (pid %lu)",
                                   (void*)gloss_.window(), (void*)surface_.hwnd(), (void*)request.target.hwnd,
                                   request.target.pid));
        facts.push_back(str_format("duplicate: %s | capture: %s | %ux%u -> %dx%d | uv %.3f,%.3f-%.3f,%.3f",
                                   report.active ? "on screen" : report.note.c_str(),
                                   cstatus.running ? "running" : cstatus.detail.c_str(), cstatus.content_width,
                                   cstatus.content_height, ostats.width, ostats.height, ostats.uv[0], ostats.uv[1],
                                   ostats.uv[2], ostats.uv[3]));
        facts.push_back(str_format("present %llu | capture frames %llu | copies %llu | empty polls %llu | "
                                   "failures %llu | pool resizes %llu",
                                   ostats.presents, cstatus.frames, cstatus.copies, cstatus.empty_polls,
                                   cstatus.failures, cstatus.pool_resizes));
        facts.push_back(str_format("paced %u fps (%s) | pass-through regions %d | panel lines %d | size agrees %s",
                                   ostats.paced_fps, ostats.active ? "active" : "idle", ostats.pass_regions,
                                   ostats.panel_lines, ostats.capture_size_agrees ? "yes" : "no"));
        facts.push_back("press Check visibility in Settings, then screenshot this window");

        std::string debug_error;
        const bool shown = debug_.present(request.target.hwnd,
                                          state_out.surface_visible ? surface_.hwnd() : nullptr,
                                          key.surface_rect, request.target.dpi, on_screen, facts,
                                          &debug_error);
        if (!shown && debug_.visible()) debug_.hide();
        if (!shown && !debug_error.empty()) {
            // Reported once per change, never per tick: a debug aid must not become
            // a log flood.
            log_warn("debug overlay: %s", debug_error.c_str());
        }
        changed = changed || shown;
    } else if (debug_.visible()) {
        debug_.hide();
        changed = true;
    }

    if (changed) {
        last_key_ = key;
        // Only record the surface as "done" when it is actually on screen: if it
        // failed, the next apply retries instead of silently giving up until
        // something else changes.
        last_key_.surface = state_out.surface_visible && surface_requested;
        last_key_.overlay = state_out.overlay_visible && overlay_requested;
        has_key_ = true;
        ++state_out.applies;
    }
    return changed;
}

void SkinEngine::reassert_stacking() {
    if (target_ == nullptr || !IsWindow(target_)) return;
    const bool surface_up = surface_.visible();
    const bool veil_up = veil_.visible();
    if (!surface_up && !veil_up) return;

    // The topmost of Azy's surfaces is the one that has to be in front of Premiere;
    // when it is, the ones below it are too (they were stacked in order).
    HWND top = surface_up ? surface_.hwnd() : veil_.hwnd();
    if (top == nullptr || window_is_above(top, target_)) return;

    log_info("stacking: Premiere was raised above Azy's surfaces; placing them back in front");
    if (surface_up) surface_.reposition(target_);
    if (veil_up) veil_.reposition(target_, surface_.visible() ? surface_.hwnd() : nullptr);
}

bool SkinEngine::probe_on_screen(std::string* detail) {
    std::string overlay_detail;
    const bool overlay_shown = veil_.visible();
    const bool overlay_ok = veil_.probe_visible(&overlay_detail);

    std::string ring_detail;
    const bool ring_shown = surface_.visible();
    const bool ring_ok = surface_.probe_visible(&ring_detail);

    // The duplicate window is checked structurally, not by reading pixels: it has
    // no redirection bitmap (the compositor paints it), so a GDI screen read is not
    // a trustworthy answer about it. What is reported instead is everything the
    // process itself knows - and it says plainly that this is not a pixel check, so
    // nobody mistakes one for the other.
    const GlossOverlay::Stats& gl = gloss_.stats();
    const CaptureStatus cs = capture_.status();
    const bool duplicate_shown = overlay_report_.active;
    std::string duplicate_detail;
    if (duplicate_shown) {
        duplicate_detail = str_format("on screen, %llu presents, %llu captured frames, %ux%u, pacing %u fps",
                                      gl.presents, cs.frames, cs.content_width, cs.content_height, gl.paced_fps);
    } else {
        duplicate_detail = "not showing: " + overlay_report_.note;
    }

    const bool any_visible = overlay_ok || ring_ok || duplicate_shown;
    std::string text = any_visible ? "the skin is on screen" : "the skin is NOT on screen";
    text += "\n";
    text += "  duplicate: " + duplicate_detail + (duplicate_shown ? " (structural, not a pixel check)" : "") + "\n";
    text += "  overlay: " + (overlay_shown ? overlay_detail : std::string("not showing")) + "\n";
    text += "  ring: " + (ring_shown ? ring_detail : std::string("not showing"));
    if (detail != nullptr) *detail = text;

    // A layer that is meant to be visible but is not: that is the interesting
    // failure, and it deserves the warning level in the log.
    if (overlay_shown && !overlay_ok) log_warn("visibility check: %s", overlay_detail.c_str());
    if (ring_shown && !ring_ok) log_warn("visibility check: %s", ring_detail.c_str());
    return any_visible;
}

}  // namespace win
}  // namespace azy
