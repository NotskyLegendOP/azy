#include "azy/core/theme.hpp"
#include <algorithm>
#include <string>

#include <cmath>

#include "azy/core/strings.hpp"

namespace azy {
namespace {

double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

unsigned char to_byte(double v) {
    const double c = clamp01(v / 255.0) * 255.0;
    return static_cast<unsigned char>(c + 0.5);
}

// Azy's charcoal ramp. Deliberately neutral (a hint of blue, never pure black)
// so it stays comfortable for long sessions and never fights Premiere's own
// colour-accurate panels.
constexpr Rgba kCharcoalLifted = {46, 47, 51, 255};   // darkness = 0
constexpr Rgba kCharcoalDeep = {11, 11, 13, 255};     // darkness = 1
constexpr Rgba kSurfaceLifted = {54, 55, 60, 255};
constexpr Rgba kSurfaceDeep = {16, 16, 19, 255};
constexpr Rgba kHighlight = {255, 255, 255, 255};
constexpr Rgba kShadow = {0, 0, 0, 255};
constexpr Rgba kText = {228, 229, 233, 255};

// The accent hues. Every one of them is a *light* colour: the accent is only ever
// used as a thin illuminated edge, so what matters is that it reads as a highlight
// against the charcoal, not that it is saturated. None of these is neon.
constexpr Rgba kAccentBlueViolet = {124, 141, 255, 255};
constexpr Rgba kAccentBlue = {88, 150, 255, 255};
constexpr Rgba kAccentViolet = {170, 126, 255, 255};
constexpr Rgba kAccentNeutral = {255, 255, 255, 255};  // = the pre-accent hairline

}  // namespace

const char* accent_name(AccentId accent) {
    switch (accent) {
        case AccentId::BlueViolet: return "Blue-violet";
        case AccentId::Blue: return "Blue";
        case AccentId::Violet: return "Violet";
        case AccentId::Neutral: return "Neutral";
    }
    return "Blue-violet";
}

const char* accent_key(AccentId accent) {
    switch (accent) {
        case AccentId::BlueViolet: return "blue_violet";
        case AccentId::Blue: return "blue";
        case AccentId::Violet: return "violet";
        case AccentId::Neutral: return "neutral";
    }
    return "blue_violet";
}

bool accent_from_key(const std::string& key, AccentId& out) {
    const std::string k = to_lower(trim(key));
    if (k == "blue_violet" || k == "blueviolet") { out = AccentId::BlueViolet; return true; }
    if (k == "blue") { out = AccentId::Blue; return true; }
    if (k == "violet" || k == "purple") { out = AccentId::Violet; return true; }
    // Both the explicit key and the empty value mean "no hue".
    if (k == "neutral" || k == "none" || k == "off" || k.empty()) { out = AccentId::Neutral; return true; }
    return false;
}

Rgba accent_color(AccentId accent) {
    switch (accent) {
        case AccentId::BlueViolet: return kAccentBlueViolet;
        case AccentId::Blue: return kAccentBlue;
        case AccentId::Violet: return kAccentViolet;
        case AccentId::Neutral: return kAccentNeutral;
    }
    return kAccentBlueViolet;
}

const char* preset_name(PresetId preset) {
    switch (preset) {
        case PresetId::Ultra: return "Ultra";
        case PresetId::Balanced: return "Balanced";
        case PresetId::Performance: return "Performance";
        case PresetId::LowPower: return "Low power";
        case PresetId::Custom: return "Custom";
    }
    return "Balanced";
}

const char* preset_key(PresetId preset) {
    switch (preset) {
        case PresetId::Ultra: return "ultra";
        case PresetId::Balanced: return "balanced";
        case PresetId::Performance: return "performance";
        case PresetId::LowPower: return "low_power";
        case PresetId::Custom: return "custom";
    }
    return "balanced";
}

bool preset_from_key(const std::string& key, PresetId& out) {
    const std::string k = to_lower(trim(key));
    if (k == "ultra") { out = PresetId::Ultra; return true; }
    if (k == "balanced" || k == "default") { out = PresetId::Balanced; return true; }
    if (k == "performance" || k == "perf") { out = PresetId::Performance; return true; }
    if (k == "low_power" || k == "lowpower" || k == "low-power") { out = PresetId::LowPower; return true; }
    if (k == "custom") { out = PresetId::Custom; return true; }
    return false;
}

PresetValues preset_values(PresetId preset) {
    // Balanced is exactly the shipped default look (1.1.x values plus the new
    // accent at its restrained default), so it is the one preset that changes
    // nothing for an existing user.
    PresetValues v;
    switch (preset) {
        case PresetId::Ultra:
            v.glass_intensity = 0.85;
            v.border_intensity = 0.80;
            v.corner_radius_dip = 12;
            v.shadow_intensity = 0.60;
            v.darkness = 0.58;
            v.overlay = true;
            v.overlay_intensity = 0.58;
            v.accent_intensity = 0.50;
            v.glow_intensity = 0.35;
            v.performance_mode = false;
            v.animations = true;
            break;
        case PresetId::Balanced:
        case PresetId::Custom:  // "Custom" applied means "back to the baseline values"
            break;
        case PresetId::Performance:
            // Fewer effects, no rounding, no shadow: the ring is at its cheapest
            // and the overlay stays, because that is what makes the skin read.
            v.glass_intensity = 0.35;
            v.border_intensity = 0.45;
            v.corner_radius_dip = 0;
            v.shadow_intensity = 0.20;
            v.darkness = 0.50;
            v.overlay = true;
            v.overlay_intensity = 0.45;
            v.accent_intensity = 0.30;
            v.glow_intensity = 0.0;
            v.performance_mode = true;
            v.animations = false;
            break;
        case PresetId::LowPower:
            // The floor: a hairline, a light tint and nothing else.
            v.glass_intensity = 0.20;
            v.border_intensity = 0.30;
            v.corner_radius_dip = 0;
            v.shadow_intensity = 0.0;
            v.darkness = 0.50;
            v.overlay = true;
            v.overlay_intensity = 0.30;
            v.accent_intensity = 0.20;
            v.glow_intensity = 0.0;
            v.performance_mode = true;
            v.animations = false;
            break;
    }
    return v;
}

Rgba mix_color(Rgba a, Rgba b, double t) {
    const double k = clamp01(t);
    Rgba out;
    out.r = to_byte(a.r * (1.0 - k) + b.r * k);
    out.g = to_byte(a.g * (1.0 - k) + b.g * k);
    out.b = to_byte(a.b * (1.0 - k) + b.b * k);
    out.a = to_byte(a.a * (1.0 - k) + b.a * k);
    return out;
}

Rgba with_alpha(Rgba c, double alpha01) {
    c.a = to_byte(clamp01(alpha01) * 255.0);
    return c;
}

Rgba lerp_darkness(Rgba light, Rgba near_black, double darkness01) {
    return mix_color(light, near_black, clamp01(darkness01));
}

const char* theme_name(ThemeId theme) {
    switch (theme) {
        case ThemeId::AzyDarkGlass: return "Azy Dark Glass";
        case ThemeId::AzyDark: return "Azy Dark";
        case ThemeId::Original: return "Original";
    }
    return "Azy Dark Glass";
}

const char* theme_key(ThemeId theme) {
    switch (theme) {
        case ThemeId::AzyDarkGlass: return "azy_dark_glass";
        case ThemeId::AzyDark: return "azy_dark";
        case ThemeId::Original: return "original";
    }
    return "azy_dark_glass";
}

bool theme_from_key(const std::string& key, ThemeId& out) {
    const std::string k = to_lower(trim(key));
    if (k == "azy_dark_glass") {
        out = ThemeId::AzyDarkGlass;
        return true;
    }
    if (k == "azy_dark") {
        out = ThemeId::AzyDark;
        return true;
    }
    if (k == "original") {
        out = ThemeId::Original;
        return true;
    }
    return false;
}

ThemePalette make_palette(ThemeId theme, const Appearance& appearance, bool dark_frame_supported) {
    ThemePalette p;
    p.theme = theme;
    p.corner_radius_dip = std::max(0, std::min(16, appearance.corner_radius_dip));

    const double glass = clamp01(appearance.glass_intensity);
    const double border = clamp01(appearance.border_intensity);
    const double shadow = clamp01(appearance.shadow_intensity);
    const double darkness = clamp01(appearance.darkness);

    const Rgba base = lerp_darkness(kCharcoalLifted, kCharcoalDeep, darkness);
    const Rgba surface = lerp_darkness(kSurfaceLifted, kSurfaceDeep, darkness);

    // The accent is resolved once here. `Neutral` (and a strength of zero) has to
    // reproduce the pre-accent look exactly, which it does because every use below
    // mixes towards a white hairline with the same weights.
    const Rgba accent_hue = accent_color(appearance.accent);
    const double accent_strength =
        appearance.accent == AccentId::Neutral ? 0.0 : clamp01(appearance.accent_intensity);
    const double glow = clamp01(appearance.glow_intensity);
    p.accent = accent_hue;
    p.accent_strength = accent_strength;
    p.glow = glow;

    // Opaque themes never use translucency; that is the whole point of Azy Dark
    // (and of Original, which is fully transparent).
    const bool glassy = theme == ThemeId::AzyDarkGlass;
    const bool visible = theme != ThemeId::Original;
    p.visible = visible;
    p.draw_surface = visible;
    if (!visible) {
        p.apply_frame_colors = false;
        p.draw_surface = false;
        p.surface_fill = with_alpha(surface, 0.0);
        p.surface_bezel = with_alpha(kHighlight, 0.0);
        p.surface_border = with_alpha(kHighlight, 0.0);
        p.surface_highlight = with_alpha(kHighlight, 0.0);
        p.surface_shadow = with_alpha(kShadow, 0.0);
        p.surface_veil = with_alpha(kShadow, 0.0);
        p.corner_radius_dip = 0;
        return p;
    }

    // Panel wash. Glassy: ~78..95% opaque so Premiere's content stays perfectly
    // readable while the surface still picks up a little of what is behind it.
    const double wash_opacity = glassy ? (0.94 - 0.16 * glass) : 1.0;
    p.surface_fill = with_alpha(surface, wash_opacity);

    // Haarlines and edges are deliberately built as a two-step ramp:
    //   depth 0: a bright 1px hairline on the frame edge (separation)
    //   depth 1: a slightly lightened 1px bezel (makes the edge read on a dark UI)
    //   depth 2+: a soft dark falloff inward (depth, without a visible bar)
    // A purely *dark* edge treatment is invisible over Premiere's own near-black
    // panels, which is why the bezel is lighter than the surface rather than
    // darker.
    // The accent reaches the two edge colours and nothing else: the 1px bezel and
    // the frame border. A glow (off by default) only raises their alpha - it never
    // widens the band, so a rested eye reads it as a lit edge rather than a halo.
    const Rgba lit = mix_color(kHighlight, accent_hue, accent_strength);
    const double lit_alpha = 1.0 + 0.55 * glow;
    p.surface_bezel = with_alpha(mix_color(lit, base, 0.70), (0.05 + 0.12 * border) * lit_alpha);
    p.surface_border = with_alpha(mix_color(lit, surface, 0.35), (0.04 + 0.09 * border) * lit_alpha);
    p.surface_highlight = with_alpha(kHighlight, 0.02 + 0.05 * border);
    p.surface_shadow = with_alpha(kShadow, 0.10 + 0.22 * shadow);
    p.shadow_enabled = shadow > 0.001;

    // The veil: one translucent sheet over the whole window. Its colour follows
    // the theme's own charcoal so the overlay reads as the same material as the
    // frame, and its strength is the slider. The floor keeps a "0%" overlay from
    // being a hard edge: 0 disables it completely, anything above starts visible.
    if (appearance.overlay && appearance.overlay_intensity > 0.001) {
        const double strength = clamp01(appearance.overlay_intensity);
        Rgba veil_color = lerp_darkness(Rgba{22, 23, 26, 255}, kCharcoalDeep, darkness);
        // A whisper of the accent in the sheet: enough that the tint belongs to the
        // same material as the lit edge, far too little to read as "a blue window".
        veil_color = mix_color(veil_color, accent_hue, 0.12 * accent_strength);
        p.surface_veil = with_alpha(veil_color, 0.05 + 0.55 * strength);
    } else {
        p.surface_veil = with_alpha(kShadow, 0.0);
    }

    // Level 1: the window frame. Only applied when the host supports it AND
    // the theme is visible.
    p.apply_frame_colors = visible && dark_frame_supported;
    p.frame_caption = mix_color(base, surface, 0.35);
    p.frame_caption.a = 255;
    p.frame_border = with_alpha(mix_color(mix_color(kHighlight, accent_hue, accent_strength * 0.6), base, 0.55),
                                0.06 + 0.10 * border);
    p.frame_text = kText;
    return p;
}

}  // namespace azy
