// Azy Skin — portable core: theme definitions and colour derivation.
//
// The skin is intentionally static: a theme is a palette plus a few derived
// translucency values. Nothing here is time-dependent, so nothing can animate.
#pragma once

#include <string>

namespace azy {

struct Rgba {
    unsigned char r = 0;
    unsigned char g = 0;
    unsigned char b = 0;
    unsigned char a = 255;
};

enum class ThemeId {
    AzyDarkGlass = 0,  // default: charcoal + subtle translucent surfaces
    AzyDark = 1,       // opaque charcoal, no translucency at all
    Original = 2,      // Azy stays out of the way entirely
};

// The accent: the single hue Azy uses for illuminated borders and the whisper of
// colour in the tint. Restraint is the point - it is a 1px hairline and a few
// percent of colour in a gradient, never a glow around a control. `Neutral` is the
// pre-1.2 look (a white hairline and untouched charcoal), so the accent can be
// turned off as well as tuned.
enum class AccentId {
    BlueViolet = 0,  // default: blue-violet, matching the reference's lighting
    Blue = 1,
    Violet = 2,
    Neutral = 3,     // no hue: the exact 1.1.x appearance
};

const char* accent_name(AccentId accent);
const char* accent_key(AccentId accent);              // stable INI identifier
bool accent_from_key(const std::string& key, AccentId& out);
Rgba accent_color(AccentId accent);                   // full-strength hue

// One of the four quality presets (spec §40). Balanced is the default and matches
// the shipped look exactly, so switching presets is never a surprise: Ultra adds
// depth, Performance and Low Power take it away.
enum class PresetId {
    Ultra = 0,
    Balanced = 1,
    Performance = 2,
    LowPower = 3,
    Custom = 4,  // not a preset: the values no longer match any of the four
};

const char* preset_name(PresetId preset);
const char* preset_key(PresetId preset);              // stable INI identifier
bool preset_from_key(const std::string& key, PresetId& out);

// Everything a preset sets. Kept as data (not code) so the settings UI, the INI
// and the tests all read the same table.
struct PresetValues {
    double glass_intensity = 0.55;
    double border_intensity = 0.60;
    int corner_radius_dip = 8;
    double shadow_intensity = 0.40;
    double darkness = 0.50;
    bool overlay = true;
    double overlay_intensity = 0.45;
    double accent_intensity = 0.35;
    double glow_intensity = 0.0;
    bool performance_mode = false;
    bool animations = false;
};

PresetValues preset_values(PresetId preset);

// User-tunable appearance knobs (0..1 unless noted). Defaults are deliberately
// conservative: the result should read as "premium dark glass", not RGB gaming.
struct Appearance {
    ThemeId theme = ThemeId::AzyDarkGlass;
    double glass_intensity = 0.55;   // how translucent Azy's own surfaces are
    double border_intensity = 0.60;  // strength of the hairline borders
    int corner_radius_dip = 8;       // 0..16 device-independent pixels
    double shadow_intensity = 0.40;  // soft inner shadow strength
    double darkness = 0.50;          // 0 = lifted charcoal, 1 = near black
    // Whole-window overlay: one translucent sheet over everything Premiere draws
    // (the "skin everything" look), plus a proportionally wider edge falloff.
    bool overlay = true;             // on by default: it is what makes the skin read
    double overlay_intensity = 0.45; // how opaque that sheet is, 0 = invisible

    // The accent, applied to the hairline/bezel and - faintly - to the overlay, so
    // the whole treatment reads as one material. `accent_intensity` is how much of
    // the hue reaches the pixels; 0 is identical to `AccentId::Neutral`.
    AccentId accent = AccentId::BlueViolet;
    double accent_intensity = 0.35;
    // Optional soft glow around the accent hairline (spec §10, §25). Off by
    // default: it is the one setting here that can look cheap if overdone.
    double glow_intensity = 0.0;
    // Spec §28. Azy's own layers are static; this only ever applied to them (the
    // widgets inside Premiere cannot be animated from outside at all), and the
    // original brief asked for no animation, so it ships off.
    bool animations = false;
};

// Everything the renderers need, resolved once per settings change.
struct ThemePalette {
    ThemeId theme = ThemeId::AzyDarkGlass;
    bool visible = true;  // false for ThemeId::Original

    // Level 1 — DWM window frame (non-client area of Premiere's top-level window).
    bool apply_frame_colors = false;
    Rgba frame_caption;
    Rgba frame_border;
    Rgba frame_text;

    // Level 2 — Azy's own click-through composition surfaces.
    bool draw_surface = false;
    Rgba surface_bezel;          // 1px raised edge just inside the frame: this is
                                 // what makes the boundary visible on a dark UI
    Rgba surface_fill;           // subtle glass wash behind the bezel
    Rgba surface_border;         // the 1px hairline on the very frame edge
    Rgba surface_highlight;      // barely-visible top inner highlight
    Rgba surface_shadow;         // soft inner shadow, max alpha at the edge
    // Uniform wash over the entire window (the overlay). Alpha is the strength; a
    // zero alpha means "no overlay", which is how the feature is switched off.
    Rgba surface_veil;
    int corner_radius_dip = 0;   // 0 = square
    bool shadow_enabled = false; // disabled in performance mode

    // The resolved accent material, exposed for the region work: `accent` is the
    // hue at full strength, `accent_strength` how much of it is in use (already
    // zeroed for `Neutral`), `glow` the requested glow (0 = none).
    Rgba accent;
    double accent_strength = 0.0;
    double glow = 0.0;
};

// Builds the palette for a theme. `dark_frame_supported` tells the palette
// whether the host can colour the window frame at all; when it cannot, the
// frame colours are simply left alone (the surface work still applies).
ThemePalette make_palette(ThemeId theme, const Appearance& appearance, bool dark_frame_supported);

const char* theme_name(ThemeId theme);
const char* theme_key(ThemeId theme);      // stable INI identifier
bool theme_from_key(const std::string& key, ThemeId& out);

// Colour helpers (exposed for unit tests).
Rgba mix_color(Rgba a, Rgba b, double t);
Rgba with_alpha(Rgba c, double alpha01);
Rgba lerp_darkness(Rgba light, Rgba near_black, double darkness01);

}  // namespace azy
