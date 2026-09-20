#include "azy/core/overlay_style.hpp"

#include <algorithm>

namespace azy {
namespace {

float channel(unsigned char value) { return static_cast<float>(value) / 255.0f; }

float clampf(float value, float lo, float hi) { return value < lo ? lo : (value > hi ? hi : value); }

}  // namespace

OverlayStyle make_overlay_style(const ThemePalette& palette, const Appearance& appearance, bool performance_mode,
                                bool rounded_corners) {
    OverlayStyle style;

    // ThemeId::Original means "Azy stays out of the way entirely". The duplicate
    // window then has nothing to add over the real one, so it is switched off
    // completely instead of being drawn as a perfect mirror (which would cost GPU
    // time to show exactly what is already on screen).
    if (!palette.visible) {
        style.visible = false;
        return style;
    }

    const bool glassy = palette.theme == ThemeId::AzyDarkGlass;

    // Darkness is remapped rather than used raw: the slider's own default (0.5)
    // has to produce a skin that is obviously there, and its maximum has to stop
    // short of crushing Premiere's text into the background. 0 -> a light
    // treatment, 1 -> 0.62 of the linear value.
    const float darkness = clampf(static_cast<float>(appearance.darkness), 0.0f, 1.0f);
    style.darkening = 0.16f + 0.46f * darkness;

    // The veil is the existing "window overlay" sheet, which already means
    // "translucent charcoal over everything Premiere draws". Slightly under its
    // own alpha: the shader adds the darkening on top, and the two together are
    // what the user saw the slider promise.
    const float veil_alpha = channel(palette.surface_veil.a);
    style.veil = veil_alpha > 0.001f ? veil_alpha * 0.85f : 0.0f;
    style.charcoal[0] = channel(palette.surface_veil.r);
    style.charcoal[1] = channel(palette.surface_veil.g);
    style.charcoal[2] = channel(palette.surface_veil.b);

    // Performance mode keeps the skin honest but drops everything that costs GPU
    // time or reads as decoration: the sheen and the grain go, the bezel and the
    // hairlines stay (they are what make the layout readable at all).
    const float glass = clampf(static_cast<float>(appearance.glass_intensity), 0.0f, 1.0f);
    const float border = clampf(static_cast<float>(appearance.border_intensity), 0.0f, 1.0f);
    const float shadow = clampf(static_cast<float>(appearance.shadow_intensity), 0.0f, 1.0f);

    style.gloss = (performance_mode || !glassy) ? 0.0f : glass * 0.20f;
    style.grain = (performance_mode || !glassy) ? 0.0f : glass * 0.012f;
    style.border = performance_mode ? std::max(0.25f, border * 0.65f) : border * 0.65f;
    style.bezel = 0.30f + 0.40f * border;
    style.depth = palette.shadow_enabled && !performance_mode ? shadow * 0.55f : 0.0f;
    style.lift = glassy ? 0.010f : 0.005f;

    style.radius_dip = rounded_corners ? static_cast<float>(palette.corner_radius_dip) : 0.0f;
    // The shader takes physical pixels and clamps to half the window, but a 16dip
    // radius at 200% DPI is 32px, which is still a rounded corner rather than the
    // 20-30px "pill" the brief rules out. The clamp keeps a future larger value
    // from turning into one.
    style.radius_dip = std::min(style.radius_dip, 16.0f);

    style.accent[0] = channel(palette.accent.r);
    style.accent[1] = channel(palette.accent.g);
    style.accent[2] = channel(palette.accent.b);
    style.accent_strength = clampf(static_cast<float>(palette.accent_strength), 0.0f, 1.0f);

    return style;
}

bool overlay_style_is_passthrough(const OverlayStyle& style) {
    if (!style.visible) return true;
    // Every visual term is effectively zero: nothing would change, so the caller
    // should not spend a capture and a composition pass proving it.
    const bool flat = style.darkening < 0.002f && style.veil < 0.002f && style.gloss < 0.002f &&
                      style.border < 0.002f && style.depth < 0.002f && style.grain < 0.002f &&
                      style.bezel < 0.002f && style.radius_dip < 0.5f;
    return flat;
}

}  // namespace azy
