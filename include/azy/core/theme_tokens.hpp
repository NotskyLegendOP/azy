// Azy Skin — portable core: the theme engine.
//
// One place decides what the skin looks like (spec §26). The renderer never
// contains a colour: the shader's constant buffer is filled from a `ThemeTokens`
// value, and every theme is a row of that table. That is what makes §28 true —
// switching themes is a uniform update, not a rebuild, and a new theme is a table
// entry plus a name.
//
// The nine themes the rebuild requires (spec §27) plus `Original` (Azy draws
// nothing, kept from earlier versions) live here. `Custom` takes its accent from
// the user's own colour and derives the rest of its lighting from it, so the
// architecture supports custom colours today rather than "later" (spec §27).
#pragma once

#include <cstddef>
#include <string>

#include "azy/core/geometry.hpp"

namespace azy {

// An 8-bit colour with alpha: what the settings file stores, what the settings
// window edits and what the colour parser returns. It never reaches the shader -
// `Rgb` below is what does.
struct Rgba {
    unsigned char r = 0;
    unsigned char g = 0;
    unsigned char b = 0;
    unsigned char a = 255;
};

// A colour in the linear-ish 0..1 space the shader works in. Deliberately not
// `Rgba`: nothing here is 8-bit, and nothing here has an alpha channel of its own
// (opacity is a separate strength value wherever it matters).
struct Rgb {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

// The token set from spec §26. Every visual component consumes these; no renderer
// contains a literal colour.
struct ThemeTokens {
    Rgb background;                // the workspace behind the panels (darkest)
    Rgb surface;                   // a panel's glass surface
    Rgb surface_secondary;         // a dock / inactive panel surface
    Rgb border;                    // the 1px border around a surface
    Rgb border_active;             // the border of the live surface (accent-led)
    Rgb accent;                    // the theme's accent
    Rgb accent_secondary;          // the second hue in the lighting
    Rgb glow;                      // what the accent's light is made of
    Rgb selection;                 // a selected item's fill
    Rgb hover;                     // a hovered surface
    Rgb text_primary;              // informational: the luminance the surfaces aim for
    Rgb text_secondary;
    Rgb text_disabled;

    // Non-colour tokens that travel with a theme.
    float surface_opacity = 0.55f;    // how much surface colour reaches the capture
    float glow_strength = 0.35f;      // localised light, never a screen-wide bloom
    float reflection_strength = 0.30f; // the polished-surface sweeps (spec §30)
    float contrast_lift = 0.06f;      // clarity recovery after darkening (spec §25)
    float base_darkening = 0.42f;     // how far the UI is pushed to the background

    // False for `Original`: Azy must show nothing at all.
    bool visible = true;
};

enum class ThemeKey {
    BluePurple = 0,  // default: the reference's blue/violet lighting on blue-black
    Cyan = 1,
    Purple = 2,
    Magenta = 3,
    Red = 4,
    Orange = 5,
    Green = 6,
    Pink = 7,
    Custom = 8,      // accent taken from the user's colour
    Original = 9,    // no skin at all
};

constexpr int kThemeCount = 10;
// The themes offered as a picker, in order (everything except Original).
constexpr int kThemeChoiceCount = 9;

const char* theme_key_name(ThemeKey key);                    // display name
const char* theme_key_id(ThemeKey key);                      // stable INI identifier
bool theme_key_from_id(const std::string& id, ThemeKey& out);
ThemeKey theme_key_at(int index);                            // for the pickers
int theme_key_index(ThemeKey key);

// The tokens for a theme. `custom_accent` is only read for ThemeKey::Custom.
//
// For `Custom`, the accent is the user's colour and the secondary/glow are derived
// from it by hue rotation and a small brightness lift, so a custom colour produces
// a coherent theme instead of a colour pasted onto a blue one.
ThemeTokens theme_tokens(ThemeKey key, Rgba custom_accent);

// Colour parsing/formatting for the settings file: "#RRGGBB", "RRGGBB" or
// "r,g,b" (0..255). Returns false when the text is not a colour.
bool parse_hex_color(const std::string& text, Rgba& out);
std::string format_hex_color(Rgba color);

// Rotates a colour's hue by `degrees` and scales its value, used to derive a
// custom theme's secondary hue from its accent.
Rgb rotate_hue(Rgb colour, float degrees, float value_scale);

}  // namespace azy
