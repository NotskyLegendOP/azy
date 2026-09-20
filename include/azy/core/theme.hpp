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

// User-tunable appearance knobs (0..1 unless noted). Defaults are deliberately
// conservative: the result should read as "premium dark glass", not RGB gaming.
struct Appearance {
    ThemeId theme = ThemeId::AzyDarkGlass;
    double glass_intensity = 0.55;   // how translucent Azy's own surfaces are
    double border_intensity = 0.60;  // strength of the hairline borders
    int corner_radius_dip = 8;       // 0..16 device-independent pixels
    double shadow_intensity = 0.40;  // soft inner shadow strength
    double darkness = 0.50;          // 0 = lifted charcoal, 1 = near black
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
    Rgba surface_fill;           // inner wash drawn over the frame border area
    Rgba surface_border;         // the 1px hairline that separates panels
    Rgba surface_highlight;      // barely-visible top inner highlight
    Rgba surface_shadow;         // soft inner shadow, max alpha at the edge
    int corner_radius_dip = 0;   // 0 = square
    bool shadow_enabled = false; // disabled in performance mode
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
