#include "azy/core/mirror_style.hpp"

#include <algorithm>

namespace azy {
namespace {

float clampf(float value, float low, float high) { return value < low ? low : (value > high ? high : value); }

// Same remap the earlier static layers used, so the "Overall darkness" slider keeps
// meaning what it meant: 0 leaves a lifted charcoal, 1 is nearly black.
float darken_remap(double darkness) { return clampf(static_cast<float>(0.16 + 0.46 * darkness), 0.0f, 0.95f); }

}  // namespace

MirrorStyle make_mirror_style(const ThemeTokens& tokens, const Appearance& appearance, bool performance_mode,
                              float dpi_scale) {
    MirrorStyle style;
    if (!tokens.visible) {
        style.visible = false;
        return style;
    }

    const float glass = clampf(static_cast<float>(appearance.glass_intensity), 0.0f, 1.0f);
    const float border = clampf(static_cast<float>(appearance.border_intensity), 0.0f, 1.0f);
    const float shadow = clampf(static_cast<float>(appearance.shadow_intensity), 0.0f, 1.0f);
    const float accent_strength = clampf(static_cast<float>(appearance.accent_intensity), 0.0f, 1.0f);
    const float glow = clampf(static_cast<float>(appearance.glow_intensity), 0.0f, 1.0f);

    // --- the material ---------------------------------------------------------
    // The theme decides how dark its base is; the slider moves it within that. The
    // result is deliberately *not* a monotone darkening: the panel surface is mixed
    // in afterwards, which is what leaves the depth the reference has (spec §13).
    style.base_dark = clampf(darken_remap(appearance.darkness) * 0.92f + tokens.base_darkening * 0.35f, 0.0f, 0.95f);
    style.surface = clampf(0.18f + tokens.surface_opacity * 0.45f, 0.0f, 0.85f);

    // Classification: what counts as "a control on a panel" rather than "panel". A
    // tighter band in performance mode (fewer pixels take the expensive path).
    style.transition = performance_mode ? 0.34f : 0.30f;
    style.boundary_soft = performance_mode ? 0.06f : 0.09f;

    // Clarity recovery follows the user's own setting where they asked for text
    // sharpness (the shadow slider doubles as "depth and definition" in the UI), and
    // otherwise keeps the theme's default.
    style.clarity = performance_mode ? 0.20f : clampf(0.26f + shadow * 0.22f, 0.0f, 0.75f);
    style.clarity_offset = 1.4f;
    style.mid_tone = 0.36f;
    style.mid_boost = performance_mode ? 0.10f : clampf(0.10f + tokens.contrast_lift, 0.0f, 0.35f);

    // Panel frames: the 1px re-lit control lines, the interior shadow, the header
    // band and the corner glow. Performance mode keeps the structure and drops what
    // costs the most (diffusion, gloss, grain) — never the readability.
    style.highlight = clampf(0.30f + border * 0.60f, 0.0f, 1.0f);
    style.shadow = performance_mode ? clampf(shadow * 0.10f, 0.0f, 0.20f) : clampf(0.06f + shadow * 0.26f, 0.0f, 0.45f);
    style.shadow_soft = 26.0f;
    style.density = performance_mode ? 0.0f : 0.05f;

    // Glass: the diffusion is a mip sample, so it is cheap enough to keep on, but it
    // is the first thing performance mode gives up.
    style.glass = performance_mode ? 0.0f : clampf(0.06f + glass * 0.46f, 0.0f, 0.70f);
    style.gloss = performance_mode ? 0.0f : clampf(0.03f + glow * 0.16f, 0.0f, 0.24f);
    style.grain = performance_mode ? 0.0f : 0.005f;
    style.key_light = performance_mode ? 0.05f : clampf(0.04f + glow * 0.10f, 0.0f, 0.20f);
    style.glow = performance_mode ? clampf(glow * 0.10f, 0.0f, 0.15f)
                                  : clampf(0.10f + tokens.glow_strength * 0.35f + glow * 0.25f, 0.0f, 0.80f);
    // Content protection is not a quality setting: a thumbnail must stay a thumbnail
    // in every mode, so performance mode keeps it.
    style.content_keep = 0.88f;
    style.vig = performance_mode ? 0.10f : 0.16f;

    // Corners and the accent. The corner radius is the user's own setting, in DIPs
    // (the UI says "8 px" and means device-independent pixels, as every other length
    // in Azy does), converted to the physical pixels the shader works in. It is
    // clamped to 0..16 DIP so it stays a corner rather than becoming a pill (16 DIP at
    // 200% is 32 physical pixels, still a corner on a 4K window); 0 means square, and
    // is the one value that is honoured literally.
    const float scale = clampf(dpi_scale, 1.0f, 3.0f);
    style.radius = appearance.corner_radius_dip > 0
                       ? clampf(static_cast<float>(appearance.corner_radius_dip), 0.0f, 16.0f) * scale
                       : 0.0f;
    style.accent_mix = clampf(accent_strength * (performance_mode ? 0.55f : 0.9f), 0.0f, 1.0f);
    style.highlight_band = 26.0f;
    style.dpi = scale;
    style.animations = appearance.animations;

    // --- the palette (spec §26: the renderer gets tokens, never literals) ------
    // `Rgb` is three contiguous floats, so a theme's colour becomes a shader vector
    // without a per-channel branch to get wrong.
    const auto copy_colour = [](float* out, const Rgb& colour) {
        out[0] = colour.r;
        out[1] = colour.g;
        out[2] = colour.b;
    };
    copy_colour(style.background, tokens.background);
    copy_colour(style.surface_colour, tokens.surface);
    copy_colour(style.border_colour, tokens.border);
    copy_colour(style.accent, tokens.accent);
    copy_colour(style.glow_colour, tokens.glow);

    // An accent strength of zero means "no hue at all" (the pre-2.0 Neutral accent):
    // the borders go neutral and the glow disappears, rather than a grey tint being
    // left behind.
    if (accent_strength <= 0.001f) {
        copy_colour(style.accent, tokens.border);
        copy_colour(style.glow_colour, tokens.border);
        style.glow = 0.0f;
        style.accent_mix = 0.0f;
    }
    return style;
}

bool mirror_style_is_passthrough(const MirrorStyle& style) {
    if (!style.visible) return true;
    // Everything that changes a pixel, in one place: if all of these are zero the
    // shader would return the capture untouched, so the pipeline is skipped instead.
    const float strength = style.base_dark + style.surface + style.highlight + style.shadow + style.glass +
                           style.gloss + style.glow + style.clarity + style.vig + style.key_light + style.grain;
    return strength <= 0.001f;
}

}  // namespace azy
