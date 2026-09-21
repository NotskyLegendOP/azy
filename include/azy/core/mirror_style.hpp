// Azy Skin — portable core: the numbers the mirror shader is given.
//
// The skin is not a second design language. Everything here comes from the theme
// engine (theme_tokens.hpp: background, surface, border, accent, glow) plus the
// sliders the user already has (darkness, glass, border, shadow, accent).
//
// Deliberately free of Windows types: the mapping from settings to shader constants
// is arithmetic, so it is tested on any host (tests/core_tests.cpp).
#pragma once

#include "azy/core/capture_math.hpp"
#include "azy/core/theme.hpp"
#include "azy/core/theme_tokens.hpp"

namespace azy {

// How many regions the shader has slots for. Declared here so the C++ layout and the
// HLSL one are checked against a single source of truth (tools/check-mirror.py).
constexpr int kMirrorPassSlots = 4;   // media regions (Program / Source / others)
// Eight: the panel model has around that many panels (menu bar, header, project,
// effects, two monitors, right column, timeline) and a frame per panel is what makes
// the workspace read as separate sheets of glass rather than one darkened picture.
constexpr int kMirrorPanelSlots = 8;  // panel frames

// Shader-ready style. Distances are physical pixels; colours are 0..1.
struct MirrorStyle {
    // --- the material ---------------------------------------------------------
    float base_dark = 0.42f;       // how far the UI is pushed to the background colour
    float surface = 0.55f;         // how much of the surface colour panel interiors take
    float transition = 0.30f;      // luminance a pixel must exceed to count as "control"
    float boundary_soft = 0.08f;   // width of that classification band
    float mid_tone = 0.34f;        // the luminance band clarity recovery targets
    float mid_boost = 0.16f;       // how much of that band is returned
    float density = 0.06f;         // interior deepening, for panel separation
    float radius = 8.0f;           // corner radius, physical pixels
    float shadow = 0.18f;          // interior shadow around panel edges
    float shadow_soft = 26.0f;     // its falloff
    float clarity = 0.32f;         // local contrast recovery (keeps text readable)
    float clarity_offset = 1.4f;   // neighbour distance for it, in DIPs
    float highlight = 0.55f;       // 1px control lines re-lit
    float gloss = 0.10f;           // the diagonal sheen
    float glass = 0.30f;           // glass diffusion (blurred backdrop mixed in)
    float glow = 0.35f;            // localised light around panel corners
    float grain = 0.006f;          // static material grain
    float accent_mix = 0.45f;      // how far borders lean towards the accent
    float highlight_band = 26.0f;  // panel header band height
    // How much of the treatment bright, colourful pixels escape: thumbnails, preview
    // images and waveform overlays are content, and the brief requires content to stay
    // faithful (spec §16). There is no API that says where they are, so this is the
    // heuristic the shader applies, and §10 of the architecture doc lists where it can
    // be wrong.
    float content_keep = 0.88f;
    float vig = 0.16f;             // edge vignette over the styled regions
    float key_light = 0.10f;       // soft key light along the top of the window
    float dpi = 1.0f;              // device pixel ratio of the target monitor

    // --- the palette ----------------------------------------------------------
    float background[3] = {0.035f, 0.043f, 0.071f};
    float surface_colour[3] = {0.071f, 0.086f, 0.141f};
    float border_colour[3] = {0.55f, 0.62f, 0.86f};
    float accent[3] = {0.486f, 0.553f, 1.0f};
    float glow_colour[3] = {0.42f, 0.36f, 0.95f};

    // False for the Original theme: Azy shows nothing at all, so nothing is captured
    // and nothing is drawn.
    bool visible = true;
    bool animations = false;
};

// Builds the style from the theme tokens plus the sliders that are not colours
// (darkness, glass, border, shadow, corner radius) and the two switches that are not
// part of the palette (performance mode, animations).
//
// `dpi_scale` is the device pixel ratio of the monitor Premiere is on (1.0 = 100%).
// The corner radius is a DIP setting and the shader works in physical pixels, so the
// conversion happens here, once, rather than being forgotten at a call site.
MirrorStyle make_mirror_style(const ThemeTokens& tokens, const Appearance& appearance, bool performance_mode,
                              float dpi_scale = 1.0f);

// True when the style would not change a single pixel: the whole pipeline (capture
// and composition) is skipped rather than drawing a no-op.
bool mirror_style_is_passthrough(const MirrorStyle& style);

}  // namespace azy
