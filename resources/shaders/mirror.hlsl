// Azy Skin — mirror composition shader (the skin itself).
//
// This pass is the difference between "a dark filter over a screenshot" and the
// reference design. It receives the live capture of the real Premiere Pro window and
// rebuilds its appearance as dark glass, while leaving the actual media untouched.
//
// How the UI is distinguished from the media — and why this is honest about it:
//
//   Premiere's workspace is drawn by Premiere, so an outside process has no list of
//   its controls. What it *does* have is the picture. The composition therefore
//   classifies each pixel the way the eye does:
//
//     * the panel model (PanelId rectangles, passed in g_panel/g_active) says where
//       the major panels are, and inside those rectangles a *luminance threshold*
//       separates "a panel surface" from "text and controls on it";
//     * the media regions (g_pass/g_pass_active: Program Monitor, Source Monitor)
//       are passed through untouched — never darkened, blurred or tinted;
//     * a local-contrast term (four neighbour taps) restores the edge detail the
//       darkening would otherwise swallow, which is what keeps text crisp;
//     * a gradient term finds the 1px control borders Premiere draws and re-lights
//       them with the theme's border colour and accent.
//
//   It is not perfect segmentation and it does not pretend to be: the diagnostics
//   say so, and docs/AZY_MIRROR_ARCHITECTURE.md §10 lists exactly where it can be
//   fooled (a dark clip thumbnail counts as surface, bright media in a panel counts
//   as control). Nothing is recreated: the pixels are Premiere's own.
//
// Everything runs in one GPU pass: point sample for the pixel-exact copy, a mip
// level for the glass diffusion, four taps for local contrast. No CPU copies, no
// screenshots, no files (spec §5, §41).
//
// Written against Shader Model 4.0 so the same source compiles with ps_4_0 and
// ps_5_0. tools/check-mirror.py enforces that this constant buffer and the C++
// `MirrorParams` struct agree member by member.

#define PASS_SLOTS 4
#define PANEL_SLOTS 8

// Luminance weights (Rec. 709), used everywhere a colour has to become "bright".
static const float3 kLuma = float3(0.2126, 0.7152, 0.0722);

cbuffer AzyMirror : register(b0) {
    float2 g_size;            // the mirror window, in physical pixels
    float2 g_uv_min;          // the part of the captured texture the window covers
    float2 g_uv_max;
    float2 g_unused;          // keeps the scalars on their register boundary
    float g_base_dark;        // 0..1  how far the UI is pushed to the background colour
    float g_surface;          // 0..1  how much of the surface colour panel interiors take
    float g_transition;       // luminance at which a pixel counts as "panel surface"
    float g_boundary_soft;    // width of that classification band
    float g_mid_tone;         // luminance band that clarity recovery targets
    float g_mid_boost;        // how much of it is returned
    float g_density;          // interior deepening, for panel separation
    float g_radius;           // corner radius, physical pixels
    float g_shadow;           // interior shadow around panel edges
    float g_shadow_soft;      // its falloff, physical pixels
    float g_clarity;          // local contrast recovery
    float g_clarity_offset;   // neighbour distance for it, device-independent pixels
    float g_highlight;        // 1px re-lit control borders
    float g_gloss;            // the diagonal sheen
    float g_glass;            // glass diffusion (blurred backdrop mixed in)
    float g_glow;             // localised glow around panel corners and edges
    float g_grain;            // static material grain
    float g_accent_mix;       // how far borders lean towards the accent
    float g_highlight_band;   // panel header band height, physical pixels
    float g_vig;              // edge vignette over the styled regions
    float g_key_light;        // the soft key light along the top of the window
    float g_dpi;              // device pixel ratio: keeps the 1px details one *DIP* wide
    float g_content_keep;     // how much of the treatment content (thumbnails) escapes
    float g_pad0;             // explicit padding: a named member the contract check can
    float g_pad1;             // compare. Twenty-eight scalars end the block exactly on the
    float g_pad2;             // 16-byte boundary the constant buffer rules require, which
    float g_pad3;             // is why the count matters as much as the values
    float g_pad4;
    float4 g_background;      // token: Background
    float4 g_surface_colour;  // token: Surface
    float4 g_border_colour;   // token: Border
    float4 g_accent;          // token: Accent
    float4 g_glow_colour;     // token: Glow
    float4 g_pass[PASS_SLOTS];    // media regions: left, top, right, bottom (window px)
    float4 g_panel[PANEL_SLOTS];  // panel rectangles, same units
    // Slot flags. Arrays of float4 only: an array of scalars packs into the registers
    // element by element, which is exactly the kind of layout a hand-written CPU struct
    // gets wrong. Four floats per vector is also how many flags fit in one register.
    float4 g_active[PANEL_SLOTS / 4];      // 1 per live panel slot
    float4 g_pass_active[PASS_SLOTS / 4];  // 1 per live media slot
};
Texture2D g_capture : register(t0);
// The same texture, sampled at a mip level: the "blurred backdrop" of the glass.
Texture2D g_capture_mip : register(t1);
SamplerState g_point : register(s0);
SamplerState g_linear : register(s1);

struct VsOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

// Full-screen triangle: no vertex buffer, no input layout to keep in sync.
VsOut vs_main(uint id : SV_VertexID) {
    VsOut output;
    float2 corner = float2((id << 1) & 2, id & 2);
    output.position = float4(corner * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    output.uv = corner;
    return output;
}

float luminance(float3 colour) { return dot(colour, kLuma); }

float sd_rounded_box(float2 pos, float2 half_extent, float radius) {
    float2 q = abs(pos) - half_extent + radius;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}

float grain(float2 pixel) {
    return frac(sin(dot(floor(pixel), float2(12.9898, 78.233))) * 43758.5453);
}

float3 sample_capture(float2 uv) { return g_capture.SampleLevel(g_point, uv, 0.0).rgb; }

float4 ps_main(VsOut input) : SV_Target {
    const float2 size = max(g_size, float2(1.0, 1.0));
    const float2 pixel = input.uv * size;                        // window pixels
    const float2 uv = g_uv_min + input.uv * (g_uv_max - g_uv_min);

    // ------------------------------------------------------------ source pixels
    const float3 captured = sample_capture(uv);
    const float captured_luma = luminance(captured);

    // ------------------------------------------------------ media pass-through
    // Inside a monitor the source pixels survive exactly: no darkening, no tint, no
    // diffusion. The ramp lives *inside* the rectangle, so even the picture's own
    // border is untouched (spec §16).
    float passthrough = 0.0;
    [unroll] for (int p = 0; p < PASS_SLOTS; ++p) {
        if (g_pass_active[p >> 2][p & 3] > 0.5) {
            const float4 rect = g_pass[p];
            const float2 to_edge = min(pixel - rect.xy, rect.zw - pixel);
            passthrough = max(passthrough, saturate(min(to_edge.x, to_edge.y) / 1.5));
        }
    }
    // Content protection: Premiere's chrome is grey (its accent colour is the one
    // saturated exception, and it sits in the control band below). A pixel that is both
    // bright *and* vividly coloured inside a panel is far more likely to be a
    // thumbnail, a preview image or a waveform overlay than a control - and the brief
    // requires those to stay faithful, with no API to say where they are. Such pixels
    // escape most of the treatment, softly, so the transition is not a visible edge.
    const float channel_max = max(captured.r, max(captured.g, captured.b));
    const float channel_min = min(captured.r, min(captured.g, captured.b));
    const float saturation = channel_max - channel_min;
    const float content = smoothstep(0.30, 0.55, saturation) * smoothstep(0.45, 0.75, captured_luma);
    const float keep = max(passthrough, content * g_content_keep);
    const float ui = 1.0 - keep;

    // --------------------------------------------------- where are we drawing?
    // Panel membership from the layout model: rectangles only, no guessing about
    // what is inside them (spec §48 of the brief: closest-possible, never faked).
    float in_panel = 0.0;
    float panel_edge = 0.0;          // 1 exactly on a panel boundary, 0 inside
    float2 panel_origin = float2(0.0, 0.0);
    float2 panel_extent = size;
    float panel_radius = g_radius;
    [unroll] for (int i = 0; i < PANEL_SLOTS; ++i) {
        if (g_active[i >> 2][i & 3] > 0.5) {
            const float4 rect = g_panel[i];
            const float2 half_extent = (rect.zw - rect.xy) * 0.5;
            const float radius = min(panel_radius, min(half_extent.x, half_extent.y) - 1.0);
            const float d = sd_rounded_box(pixel - (rect.xy + rect.zw) * 0.5, half_extent, radius);
            const float inside = saturate(0.5 - d);
            if (inside > in_panel) {
                in_panel = inside;
                panel_origin = rect.xy;
                panel_extent = rect.zw - rect.xy;
            }
            // A 1px ring on the boundary, and the shadow just inside it.
            panel_edge = max(panel_edge, saturate(1.0 - abs(d) / 1.0) * step(-1.0, d));
        }
    }

    // Panel interiors are classified by luminance: below the threshold it is the
    // panel's own surface, above it is text or a control. Panels without a modelled
    // rectangle fall back to the whole workspace (`in_panel` == 0).
    const float surface_membership = max(in_panel, saturate(1.0 - passthrough));
    const float is_surface = 1.0 - smoothstep(g_transition - g_boundary_soft, g_transition + g_boundary_soft,
                                              captured_luma);

    // ------------------------------------------------------------- the material
    // 1. Glass diffusion: a blurred version of the same pixels is mixed in, so light
    //    from bright content spreads through the panel the way it does through real
    //    glass. Weighted by the surface classification, so text does not smudge.
    float3 colour = captured;
    const float3 diffused = g_capture_mip.SampleLevel(g_linear, uv, 4.0).rgb;
    colour = lerp(colour, diffused, saturate(g_glass * is_surface * surface_membership * ui));

    // 2. The dark base. Applied to everything except the media regions.
    colour = lerp(colour, g_background.rgb, saturate(g_base_dark * ui));

    // 3. The panel surface: interiors take the theme's surface colour, which is what
    //    turns a dimmed screenshot into a set of dark glass panels (spec §14).
    colour = lerp(colour, g_surface_colour.rgb, saturate(g_surface * is_surface * surface_membership * ui));

    // 5. Clarity: the darkening took contrast away from text. Four neighbour taps
    //    restore it locally — cheap, GPU-only, and the reason labels stay readable at
    //    any theme's darkness (spec §25).
    if (ui > 0.001) {
        // The neighbour distance is a DIP setting: on a 200% display the same
        // tactile DIP distance is twice as many physical pixels.
        const float2 step_px = (g_clarity_offset * max(g_dpi, 1.0)) / size;
        const float3 north = sample_capture(uv + float2(0.0, -step_px.y));
        const float3 south = sample_capture(uv + float2(0.0, step_px.y));
        const float3 west = sample_capture(uv - float2(step_px.x, 0.0));
        const float3 east = sample_capture(uv + float2(step_px.x, 0.0));
        const float local_mean = (luminance(north) + luminance(south) + luminance(west) + luminance(east)) * 0.25;
        // The same neighbours drive the border detector: a hard 1px line has a large
        // local gradient, a flat panel has almost none.
        const float gradient = max(max(abs(captured_luma - local_mean) * 2.0, 0.0),
                                   max(abs(luminance(north) - captured_luma), abs(luminance(east) - captured_luma)));
        colour += (captured_luma - local_mean) * g_clarity * (0.5 + 0.5 * is_surface) * ui;

        // Mid-tone recovery: text sits in the middle luminance band. Bringing that
        // band back up is what keeps the skin from looking like a dimmer.
        const float band = smoothstep(0.06, g_mid_tone, local_mean) * (1.0 - smoothstep(g_mid_tone, 0.95, local_mean));
        colour += colour * g_mid_boost * band * ui;

        // 6. Borders: Premiere's own control lines, re-lit in the theme's border
        //    colour (accent-leaning on active surfaces).
        const float edge = saturate(gradient / max(g_transition, 0.05)) * g_highlight;
        const float3 border_ink = lerp(g_border_colour.rgb, g_accent.rgb, saturate(g_accent_mix));
        colour += border_ink * edge * ui;
    }

    // ---------------------------------------------------------------- panel frame
    // Each modelled panel gets the reference's treatment: a soft interior shadow so
    // it reads as a sheet of glass, a 1px rounded border, a faint accent band where
    // its header is, and a glow in its top corner.
    if (panel_edge > 0.0 && ui > 0.001) {
        const float ring = panel_edge * g_highlight;
        const float3 border_ink = lerp(g_border_colour.rgb, g_accent.rgb, saturate(g_accent_mix * 0.8));
        colour += border_ink * ring;

        // Header band: a low-opacity accent wash across the top of the panel.
        const float band = saturate(1.0 - (pixel.y - panel_origin.y) / max(g_highlight_band, 1.0));
        colour += g_accent.rgb * band * 0.05 * saturate(g_glow * 2.0) * in_panel;

        // Corner glow: a soft light at the panel's top-left, exactly the "decorative
        // lighting" the reference uses to make the glass look lit from above.
        const float2 corner_delta = (pixel - panel_origin) / max(panel_extent, float2(1.0, 1.0));
        const float corner_light = saturate(1.0 - length(corner_delta * float2(2.2, 3.4)) * 2.0);
        colour += g_glow_colour.rgb * corner_light * corner_light * g_glow * in_panel;
    }

    // Interior shadow: a short falloff inside every panel edge. This is the "soft
    // depth" of the reference — panels look inset rather than pasted on.
    if (ui > 0.001) {
        const float2 edge_distance = min(pixel, size - pixel) / max(g_shadow_soft, 1.0);
        const float inset = saturate(min(edge_distance.x, edge_distance.y));
        // The depth is the shadow plus the density token: the shadow is the visible
        // inset, the density a slight extra deepening of the interiors, which is what
        // separates neighbouring panels without drawing a line between them (spec §15).
        colour *= 1.0 - (g_shadow + g_density) * (1.0 - inset) * g_surface * 2.0;
    }

    // ------------------------------------------------------------ global lighting
    // The reference's light comes from above: a wide, very low-contrast key light
    // over the top of the window, and a diagonal sheen across the whole surface
    // (spec §30). Both are per-pixel and static, so they cost nothing to animate.
    const float2 normalized = pixel / size;
    const float key = saturate(1.0 - normalized.y * 1.6);
    colour += g_glow_colour.rgb * key * key * g_key_light * ui;

    const float sweep = saturate(1.0 - abs(normalized.x * 0.55 + normalized.y * 0.45 - 0.34) * 2.6);
    colour += g_gloss * sweep * sweep * ui;

    // Second, tighter reflection: the "polished" highlight of the glass.
    const float polish = saturate(1.0 - abs(normalized.x - 0.18) * 3.4) * saturate(1.0 - normalized.y * 2.2);
    colour += g_gloss * polish * polish * 0.35 * ui;

    // Vignette: pulls the eye inward, keeps the window edges dark.
    const float2 half_size = size * 0.5;
    const float radius_norm = length(pixel - half_size) / max(length(half_size), 1.0);
    colour *= 1.0 - g_vig * saturate(radius_norm * radius_norm) * ui;

    // Material grain: one 1/255-level dither so the large flat panels do not band.
    if (g_grain > 0.0) {
        colour += (grain(pixel) - 0.5) * g_grain * ui;
    }

    // ------------------------------------------------------------- the boundary
    // Corner rounding on the window itself, then premultiplied alpha for the
    // composition swap chain.
    float alpha = 1.0;
    if (g_radius > 0.5) {
        const float2 half_extent = half_size - 0.5;
        const float distance_to_edge =
            sd_rounded_box(pixel - half_size, half_extent, min(g_radius, min(size.x, size.y) * 0.5 - 1.0));
        alpha = saturate(0.5 - distance_to_edge);
        if (alpha <= 0.004) return float4(0.0, 0.0, 0.0, 0.0);
    }

    // Applied last so media pixels leave this shader as the bytes they arrived as.
    colour = lerp(captured, saturate(colour), ui);
    return float4(colour * alpha, alpha);
}
