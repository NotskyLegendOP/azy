// Azy Skin — portable core: the numbers the overlay shader is given.
//
// The skin of the duplicate window is not a second design language. Every value
// here is derived from the palette the rest of Azy already uses (theme.cpp's
// charcoal ramp, the accent, the veil) and from the same sliders in the settings
// window, so "Overall darkness" moves the mirror and the frame together and the
// user never has to learn a second set of controls.
//
// Deliberately free of Windows types: the mapping from settings to shader
// constants is arithmetic, so it is tested on any host.
#pragma once

#include "azy/core/theme.hpp"

namespace azy {

// Shader-ready style. Distances are device-independent pixels; colours are 0..1.
struct OverlayStyle {
    // How far the UI is pushed towards charcoal: 0 = untouched capture,
    // 0.9 = nearly black. This is the "Overall darkness" slider, remapped.
    float darkening = 0.38f;
    // How much of the charcoal colour itself is mixed in. The difference between
    // "dimmed" and "skinned".
    float veil = 0.25f;
    // The soft diagonal sheen. Glass, not plastic: low contrast, wide.
    float gloss = 0.11f;
    // Panel separators (1px hairlines on the layout model's panel edges).
    float border = 0.39f;
    // Edge vignette depth.
    float depth = 0.22f;
    // Static grain. Small enough to read as material, never as noise.
    float grain = 0.006f;
    // Corner radius in device-independent pixels (0 = square corners).
    float radius_dip = 8.0f;
    // The 1px lighter bezel the whole visual language is built on.
    float bezel = 0.56f;
    // How far the blacks are lifted, which is what makes it glass instead of a
    // dark filter.
    float lift = 0.010f;
    float accent[3] = {0.486f, 0.553f, 1.0f};
    float accent_strength = 0.35f;
    // The colour the veil mixes towards.
    float charcoal[3] = {0.086f, 0.090f, 0.102f};
    // False for ThemeId::Original: Azy must then show nothing at all, which the
    // renderer implements by not drawing and not capturing.
    bool visible = true;
};

// Builds the style from the resolved palette plus the two switches that are not
// part of the palette: performance mode (no GPU extras) and the rounded-corners
// feature key.
OverlayStyle make_overlay_style(const ThemePalette& palette, const Appearance& appearance, bool performance_mode,
                                bool rounded_corners);

// True when the style would not change a single pixel: the renderer skips the
// whole pipeline (no capture, no composition) rather than drawing a no-op.
bool overlay_style_is_passthrough(const OverlayStyle& style);

}  // namespace azy
