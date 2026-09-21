// Azy Skin — portable core: colours, and the settings that shape them.
//
// This file used to hold the static skin's palette (a charcoal ramp, a hairline, a
// veil). The rebuild moved the look into the theme engine (theme_tokens.hpp), which
// is where every colour now lives, so what remains here are the small colour
// helpers that other portable code shares and the appearance settings themselves.
#pragma once

#include <string>

#include "azy/core/geometry.hpp"
#include "azy/core/theme_tokens.hpp"

namespace azy {

// `Rgba`, `Rgb` and the theme tokens all live in theme_tokens.hpp, which this
// header includes: the settings and the theme engine describe the same colours, so
// there is one definition of them.

// The user-facing appearance settings. Everything the renderer does comes from here
// plus the theme engine's tokens; nothing in the renderer has a colour of its own.
struct Appearance {
    // Which of the nine themes (plus Original) is active. `custom_accent` is read
    // only for ThemeKey::Custom, and is what makes spec §27's "custom" real rather
    // than an unused enum value.
    ThemeKey theme = ThemeKey::BluePurple;
    Rgba custom_accent{124, 140, 255, 255};

    // How much glass: the diffusion that makes a panel read as frosted rather than
    // merely darkened (spec §14).
    double glass_intensity = 0.55;
    // Strength of the 1px control borders and the panel frames (spec §15).
    double border_intensity = 0.60;
    // Corner radius in device-independent pixels, 0..16 (spec §15).
    int corner_radius_dip = 8;
    // Interior shadows: the depth that makes a panel look inset (spec §14).
    double shadow_intensity = 0.40;
    // How far the whole UI is pushed towards the theme's background colour.
    double darkness = 0.50;

    // How much of the theme's accent reaches borders, panel frames and glow, and how
    // much light the accent's glow carries (spec §29).
    double accent_intensity = 0.55;
    double glow_intensity = 0.25;
    // Subtle transitions: the skin eases in when it appears and cross-fades when the
    // theme changes (spec §31). Never continuous, never clocked - and off by default,
    // because the brief asks for "no animation by default": the skin appears fully
    // formed, and the Ultra preset is where a transition belongs.
    bool animations = false;
};

// --- small colour helpers shared by portable code ---------------------------

Rgba mix_color(Rgba a, Rgba b, double t);
Rgba with_alpha(Rgba c, double alpha01);

}  // namespace azy
