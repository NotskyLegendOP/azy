// Azy Skin — overlay composition shader.
//
// One full-window pass that turns the captured image of Premiere's window into a
// glossy dark duplicate of it and hands it to the compositor with premultiplied
// alpha. Everything the skin does visually happens here, which is why this file
// exists as a real .hlsl file: it is embedded into the executable at build time by
// CMake (see tools/check-overlay.py, which also enforces that the slot
// counts below match the C++ side).
//
// Written against Shader Model 4.0 so the same source compiles with either
// profile: d3dcompiler_46 and older only offer ps_4_0, the newer ones prefer
// ps_5_0. Nothing here needs 5.0 - no gathers, no dynamic constant-buffer
// indexing ([unroll] turns the region loops into literals), no wave ops.
//
// What the pass does, in order:
//   1. Samples the mirror of Premiere's window through the sub-rectangle the
//      overlay actually covers.
//   2. Leaves the picture regions (Program and Source monitors) untouched: the
//      footage must never be darkened.
//   3. Darkens and tints everything else into the Azy charcoal, lifts the blacks
//      a little so it reads as glass rather than as a dimmed screenshot, adds a
//      soft diagonal sheen and an edge vignette for depth.
//   4. Draws 1px panel separators and a 1px lighter frame around the window.
//   5. Cuts the corners with a rounded-rectangle signed distance field and emits
//      premultiplied alpha.

#define PASS_SLOTS 4
#define PANEL_SLOTS 4
#define FEATHER 1.5

// The members below are float4/float2/scalars in this exact order, and the C++
// mirror (AzyParams in gloss_overlay.cpp) pads identically. No float3 appears
// anywhere: a float3 would let the compiler insert padding the CPU side cannot
// see, and tools/check-overlay.py fails the build if one reappears.
cbuffer AzyParams : register(b0) {
    float2 g_size;            // window size in physical pixels
    float2 g_uv_min;          // top-left of the overlay inside the captured texture
    float2 g_uv_max;          // bottom-right
    float2 g_unused;          // keeps the scalars below on their register boundary
    float g_darkness;         // 0..0.9  how far the UI is pushed to charcoal
    float g_veil;             // 0..0.35 charcoal tint mixed over the UI
    float g_gloss;            // 0..0.5  diagonal sheen strength
    float g_border;           // 0..1    panel separator strength
    float g_depth;            // 0..0.6  edge vignette
    float g_grain;            // 0..0.02 static glass grain (0 = off)
    float g_radius;           // corner radius in physical pixels (0 = square)
    float g_bezel;            // 0..1    the 1px lighter frame
    float g_dpi;              // device pixel ratio, so 1px stays 1 physical pixel
    float g_lift;             // how much the blacks are lifted (glass, not film)
    float4 g_charcoal;        // rgb = the charcoal the veil mixes towards
    float4 g_pass[PASS_SLOTS];   // picture regions: left, top, right, bottom in pixels
    float4 g_panel[PANEL_SLOTS]; // panel rectangles to outline, same units
    float4 g_active;             // x,y,z,w = 1 when the matching panel slot is live
    float4 g_pass_active;        // x,y,z,w = 1 when the matching pass slot is live
    float4 g_accent;             // rgb = accent tint, a = its strength (0..1)
};

Texture2D g_capture : register(t0);
SamplerState g_sampler : register(s0);

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

float sd_rounded_box(float2 point, float2 half_extent, float radius) {
    float2 q = abs(point) - half_extent + radius;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}

// Hash noise, kept to a handful of instructions: this runs for every pixel of a
// full-screen pass and only feeds an optional 1/255-level grain.
float grain(float2 pixel) {
    return frac(sin(dot(floor(pixel), float2(12.9898, 78.233))) * 43758.5453);
}

float4 ps_main(VsOut input) : SV_Target {
    const float2 size = max(g_size, float2(1.0, 1.0));
    const float2 pixel = input.uv * size;             // pixel centre in the window
    const float2 uv = g_uv_min + input.uv * (g_uv_max - g_uv_min);
    const float3 captured = g_capture.SampleLevel(g_sampler, uv, 0.0).rgb;

    // ---------------------------------------------------------------- corners
    float alpha = 1.0;
    if (g_radius > 0.5) {
        const float2 half_extent = size * 0.5 - 0.5;
        const float distance_to_edge =
            sd_rounded_box(pixel - size * 0.5, half_extent, min(g_radius, min(size.x, size.y) * 0.5 - 1.0));
        alpha = saturate(0.5 - distance_to_edge);
        if (alpha <= 0.004) return float4(0.0, 0.0, 0.0, 0.0);
    }

    // -------------------------------------------------- picture pass-through
    // Any pixel inside a monitor keeps exactly what Premiere drew. FEATHER is the
    // width of the transition: inside the rectangle the value is 1 (nothing is
    // touched at all), and it ramps to 0 over the last FEATHER pixels at the edge,
    // so the boundary between skinned UI and untouched footage is not a hard line.
    // The ramp lives *inside* the rectangle, which is why the picture is never
    // darkened, not even at its border.
    float passthrough = 0.0;
    [unroll] for (int p = 0; p < PASS_SLOTS; ++p) {
        if (g_pass_active[p] > 0.5) {
            const float4 rect = g_pass[p];
            const float2 to_edge = min(pixel - rect.xy, rect.zw - pixel);
            const float inside = saturate(min(to_edge.x, to_edge.y) / FEATHER);
            passthrough = max(passthrough, inside);
        }
    }
    const float ui = 1.0 - passthrough;

    // ------------------------------------------------------------ the skin
    // Push the UI towards charcoal, mix in a trace of the charcoal colour itself
    // (that is what stops large flat areas from looking merely dimmed), then lift
    // the blacks so the result reads as dark glass.
    float3 colour = lerp(captured, g_charcoal, g_veil);
    colour *= 1.0 - g_darkness * ui;
    colour = lerp(colour, min(colour + g_lift, 1.0), ui);

    // Soft diagonal sheen: a wide, low-contrast sweep. Two exponentials, no
    // texture lookups, no noise texture to ship.
    const float2 normalized = pixel / size;
    const float sweep = saturate(1.0 - abs(normalized.x * 0.55 + normalized.y * 0.45 - 0.30) * 3.2);
    colour += g_gloss * sweep * sweep * ui;

    // Accent: the brief asks for a subtle blue/violet note, not a neon edge, so
    // it is strongest at the top of the window where the header sits.
    const float accent_falloff = saturate(1.0 - normalized.y * 2.2);
    colour += g_accent.rgb * (g_accent.a * 0.16 * accent_falloff * ui);

    // Depth: a gentle vignette from the window edge inwards.
    const float edge_distance = min(min(pixel.x, pixel.y), min(size.x - pixel.x, size.y - pixel.y));
    const float vignette = saturate(edge_distance / (16.0 * g_dpi));
    colour *= lerp(1.0 - g_depth, 1.0, vignette);

    // ------------------------------------------------------- panel separators
    // 1px hairlines on the panel boundaries the layout model knows about. They
    // are drawn around both sides of the edge, which is what makes neighbouring
    // panels look separated rather than outlined.
    [unroll] for (int i = 0; i < PANEL_SLOTS; ++i) {
        if (g_active[i] > 0.5) {
            const float4 rect = g_panel[i];
            const float dx = min(pixel.x - rect.x, rect.z - pixel.x);
            const float dy = min(pixel.y - rect.y, rect.w - pixel.y);
            const float d = min(dx, dy);
            // 1 *physical* pixel wide, at every DPI: a separator that doubled on a
            // 200% display would read as a thick line, not as a hairline.
            const float line = saturate(1.0 - abs(d));
            colour = lerp(colour, colour + 0.055, line * g_border * ui);
        }
    }

    // ------------------------------------------------------------- glass frame
    // The 1px lighter bezel the whole visual language is built on, plus a trace
    // of the accent along the top edge.
    // Also one physical pixel: the bezel is the boundary of the mirror, and it has
    // to agree with the frame it is drawn over.
    const float bezel = saturate(1.0 - edge_distance / 1.5) * g_bezel * alpha;
    colour += bezel * (0.075 + g_accent.rgb * g_accent.a * 0.05);

    if (g_grain > 0.0) {
        colour += (grain(pixel) - 0.5) * g_grain * ui;
    }

    colour = saturate(colour);
    // DWM expects premultiplied alpha for a composition swap chain.
    return float4(colour * alpha, alpha);
}
