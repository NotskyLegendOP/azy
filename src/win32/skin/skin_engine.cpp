#include "azy/win32/skin/skin_engine.hpp"

#include "azy/core/log.hpp"
#include "azy/core/strings.hpp"
#include "azy/win32/os/win_util.hpp"
#include "azy/win32/skin/input_guard.hpp"

namespace azy {
namespace win {
namespace {

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
    const int thickness = std::max(band_px + 2, radius_px + 1);
    const int required = thickness * 2 + 2;
    return frame.width() >= required && frame.height() >= required;
}

// Band thickness of the composition surface, in physical pixels: how far the
// edge treatment reaches into the window. Bounded so a small floating window
// cannot be swallowed by its own decoration, and kept thin in performance mode,
// where the treatment is a static edge rather than a shaded one.
int ring_band_px(const SkinRequest& request) {
    const int nominal = request.performance_mode ? dip_to_px(3, request.target.dpi)
                                                 : dip_to_px(10, request.target.dpi);
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
}

void SkinEngine::revert() {
    composer_.revert();
    surface_.hide();
    has_key_ = false;
    last_key_ = VisualKey{};
}

void SkinEngine::release_surface() {
    surface_.destroy();
    log_debug("composition surface destroyed");
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

    // The frame keeps its DWM treatment while the window merely exists: those
    // attributes cost nothing while Premiere is minimised or hidden, and
    // re-applying them on restore would flash the original title bar. Only a
    // disabled skin (or a gone window) removes them.
    const bool keep_frame = want_skin && window_alive;

    if (!keep_frame) {
        const bool had_frame = composer_.is_applied();
        const bool had_surface = surface_.visible();
        if (had_frame) {
            composer_.revert();
            state_out.frame_applied = false;
        }
        if (had_surface) {
            surface_.hide();
            state_out.surface_visible = false;
        }
        has_key_ = false;
        if (had_frame || had_surface) {
            log_info("skin removed (%s)", suspend_reason_name(request.suspend));
        }
        return had_frame || had_surface;
    }

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
    const bool surface_requested =
        key.surface && request.suspend == SuspendReason::None && request.target.valid();
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

    if (surface_requested && !window_large_enough_for_ring(key.surface_rect, key.band_px, key.radius_px)) {
        // Too small to decorate: hide the ring and leave the frame treatment in
        // place. Not counted as a failure.
        if (surface_.visible()) surface_.hide();
        state_out.surface_visible = false;
        if (changed || has_key_) {
            last_key_ = key;
            last_key_.surface = false;
            has_key_ = true;
        }
        return changed;
    }

    if (surface_requested && surface_changed) {
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

    if (changed) {
        last_key_ = key;
        // Only record the surface as "done" when it is actually on screen: if it
        // failed, the next apply retries instead of silently giving up until
        // something else changes.
        last_key_.surface = state_out.surface_visible && surface_requested;
        has_key_ = true;
        ++state_out.applies;
    }
    return changed;
}

}  // namespace win
}  // namespace azy
