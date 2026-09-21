// Azy Skin — dumps the theme tokens and the mirror style as text.
//
// The renderer's numbers live in C++ (theme_tokens.cpp, mirror_style.cpp) and the
// shader's maths lives in HLSL. Neither can be reviewed on a machine without a GPU,
// so tools/preview_render.py needs the *real* numbers rather than a transcription of
// them: it runs this tool and composites the result. A preview produced that way can
// only be wrong if the HLSL port in the script is wrong - it can never disagree with
// the application about what a theme is.
//
//   azy_dump_style                 # every theme, default sliders
//   azy_dump_style --theme green --darkness 0.7 --glass 0.4
//
// Output is one `key value` pair per line, with a blank line between sections.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "azy/core/capture_math.hpp"
#include "azy/core/mirror_style.hpp"
#include "azy/core/panel_map.hpp"
#include "azy/core/theme.hpp"
#include "azy/core/theme_tokens.hpp"

namespace {

void print_rgb(const char* prefix, const azy::Rgb& colour) {
    std::printf("%s.r %.6f\n%s.g %.6f\n%s.b %.6f\n", prefix, colour.r, prefix, colour.g, prefix, colour.b);
}

void print_tokens(const azy::ThemeTokens& tokens) {
    print_rgb("token.background", tokens.background);
    print_rgb("token.surface", tokens.surface);
    print_rgb("token.surface_secondary", tokens.surface_secondary);
    print_rgb("token.border", tokens.border);
    print_rgb("token.border_active", tokens.border_active);
    print_rgb("token.accent", tokens.accent);
    print_rgb("token.accent_secondary", tokens.accent_secondary);
    print_rgb("token.glow", tokens.glow);
    print_rgb("token.selection", tokens.selection);
    print_rgb("token.hover", tokens.hover);
    print_rgb("token.text_primary", tokens.text_primary);
    print_rgb("token.text_secondary", tokens.text_secondary);
    print_rgb("token.text_disabled", tokens.text_disabled);
    std::printf("token.surface_opacity %.6f\n", tokens.surface_opacity);
    std::printf("token.glow_strength %.6f\n", tokens.glow_strength);
    std::printf("token.reflection_strength %.6f\n", tokens.reflection_strength);
    std::printf("token.contrast_lift %.6f\n", tokens.contrast_lift);
    std::printf("token.base_darkening %.6f\n", tokens.base_darkening);
    std::printf("token.visible %d\n", tokens.visible ? 1 : 0);
}

void print_style(const azy::MirrorStyle& style) {
    std::printf("style.base_dark %.6f\n", style.base_dark);
    std::printf("style.surface %.6f\n", style.surface);
    std::printf("style.transition %.6f\n", style.transition);
    std::printf("style.boundary_soft %.6f\n", style.boundary_soft);
    std::printf("style.mid_tone %.6f\n", style.mid_tone);
    std::printf("style.mid_boost %.6f\n", style.mid_boost);
    std::printf("style.density %.6f\n", style.density);
    std::printf("style.radius %.6f\n", style.radius);
    std::printf("style.shadow %.6f\n", style.shadow);
    std::printf("style.shadow_soft %.6f\n", style.shadow_soft);
    std::printf("style.clarity %.6f\n", style.clarity);
    std::printf("style.clarity_offset %.6f\n", style.clarity_offset);
    std::printf("style.highlight %.6f\n", style.highlight);
    std::printf("style.gloss %.6f\n", style.gloss);
    std::printf("style.glass %.6f\n", style.glass);
    std::printf("style.glow %.6f\n", style.glow);
    std::printf("style.grain %.6f\n", style.grain);
    std::printf("style.accent_mix %.6f\n", style.accent_mix);
    std::printf("style.highlight_band %.6f\n", style.highlight_band);
    std::printf("style.vig %.6f\n", style.vig);
    std::printf("style.key_light %.6f\n", style.key_light);
    std::printf("style.dpi %.6f\n", style.dpi);
    std::printf("style.content_keep %.6f\n", style.content_keep);
    std::printf("style.visible %d\n", style.visible ? 1 : 0);
    std::printf("style.animations %d\n", style.animations ? 1 : 0);
    std::printf("style.passthrough %d\n", azy::mirror_style_is_passthrough(style) ? 1 : 0);
    std::printf("style.background.r %.6f\n", style.background[0]);
    std::printf("style.background.g %.6f\n", style.background[1]);
    std::printf("style.background.b %.6f\n", style.background[2]);
    std::printf("style.surface_colour.r %.6f\n", style.surface_colour[0]);
    std::printf("style.surface_colour.g %.6f\n", style.surface_colour[1]);
    std::printf("style.surface_colour.b %.6f\n", style.surface_colour[2]);
    std::printf("style.border_colour.r %.6f\n", style.border_colour[0]);
    std::printf("style.border_colour.g %.6f\n", style.border_colour[1]);
    std::printf("style.border_colour.b %.6f\n", style.border_colour[2]);
    std::printf("style.accent.r %.6f\n", style.accent[0]);
    std::printf("style.accent.g %.6f\n", style.accent[1]);
    std::printf("style.accent.b %.6f\n", style.accent[2]);
    std::printf("style.glow_colour.r %.6f\n", style.glow_colour[0]);
    std::printf("style.glow_colour.g %.6f\n", style.glow_colour[1]);
    std::printf("style.glow_colour.b %.6f\n", style.glow_colour[2]);
}

// The window-local geometry the renderer actually hands the shader: the panel model
// for the client area, the media pass-through rectangles and the panel frames. These
// come from the same functions the application calls, so a preview can never disagree
// with the layout model the skin uses. `overlay` is the window Azy places (the client
// area grown by the invisible resize border, which is what a maximized window has).
void dump_regions(int client_width, int client_height, double dpi_scale, const std::string& workspace,
                  int overlay_margin, int radius_px) {
    if (client_width <= 0 || client_height <= 0) return;
    const unsigned dpi = static_cast<unsigned>(96.0 * dpi_scale + 0.5);
    azy::WorkspaceId profile = azy::WorkspaceId::Auto;
    if (!workspace.empty()) azy::workspace_from_key(workspace, profile);

    const azy::Rect client = azy::Rect::from_size(0, 0, client_width, client_height);
    const std::vector<azy::PanelRect> panels = azy::build_panel_map(client, dpi, profile, radius_px);
    const azy::Rect overlay = azy::Rect::from_size(-overlay_margin, -overlay_margin,
                                                  client_width + overlay_margin * 2,
                                                  client_height + overlay_margin * 2);
    const azy::Rect origin{0, 0, 0, 0};
    const std::vector<azy::LocalRect> media = azy::monitor_pass_through(panels, overlay, origin, dpi);
    const std::vector<azy::LocalRect> frames =
        azy::panel_hairlines(panels, overlay, origin, media, azy::kMirrorPanelSlots);

    std::printf("geometry.client %d %d\n", client_width, client_height);
    std::printf("geometry.overlay %d %d %d %d\n", overlay.left, overlay.top, overlay.width(), overlay.height());
    std::printf("geometry.dpi %u\n", dpi);
    std::printf("geometry.radius %d\n", radius_px);
    for (const azy::PanelRect& panel : panels) {
        std::printf("panel %d %d %d %d %d %d\n", static_cast<int>(panel.id), panel.rect.left, panel.rect.top,
                    panel.rect.width(), panel.rect.height(), panel.usable ? 1 : 0);
    }
    for (std::size_t i = 0; i < media.size(); ++i) {
        std::printf("region.media %zu %.2f %.2f %.2f %.2f\n", i, media[i].left, media[i].top, media[i].right,
                    media[i].bottom);
    }
    for (std::size_t i = 0; i < frames.size(); ++i) {
        std::printf("region.panel %zu %.2f %.2f %.2f %.2f\n", i, frames[i].left, frames[i].top, frames[i].right,
                    frames[i].bottom);
    }
    std::printf("--\n");
}

double number_arg(int argc, char** argv, const char* name, double fallback) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], name) == 0) return std::atof(argv[i + 1]);
    }
    return fallback;
}

}  // namespace

int main(int argc, char** argv) {
    azy::Appearance appearance;
    appearance.darkness = number_arg(argc, argv, "--darkness", appearance.darkness);
    appearance.glass_intensity = number_arg(argc, argv, "--glass", appearance.glass_intensity);
    appearance.border_intensity = number_arg(argc, argv, "--border", appearance.border_intensity);
    appearance.shadow_intensity = number_arg(argc, argv, "--shadow", appearance.shadow_intensity);
    appearance.accent_intensity = number_arg(argc, argv, "--accent", appearance.accent_intensity);
    appearance.glow_intensity = number_arg(argc, argv, "--glow", appearance.glow_intensity);
    appearance.corner_radius_dip = static_cast<int>(number_arg(argc, argv, "--radius", appearance.corner_radius_dip));
    appearance.animations = number_arg(argc, argv, "--animations", 1.0) > 0.5;
    const bool performance_mode = number_arg(argc, argv, "--performance", 0.0) > 0.5;
    const double dpi_scale = number_arg(argc, argv, "--dpi", 1.0);

    std::string wanted;
    std::string workspace;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--theme") == 0) wanted = argv[i + 1];
        if (std::strcmp(argv[i], "--workspace") == 0) workspace = argv[i + 1];
    }

    const int client_width = static_cast<int>(number_arg(argc, argv, "--client-w", 0));
    const int client_height = static_cast<int>(number_arg(argc, argv, "--client-h", 0));
    const int overlay_margin = static_cast<int>(number_arg(argc, argv, "--overlay-margin", 0));
    if (client_width > 0 && client_height > 0) {
        dump_regions(client_width, client_height, dpi_scale, workspace, overlay_margin,
                     static_cast<int>(number_arg(argc, argv, "--radius", appearance.corner_radius_dip)) * dpi_scale);
    }

    for (int i = 0; i < azy::kThemeCount; ++i) {
        const azy::ThemeKey key = azy::theme_key_at(i);
        if (!wanted.empty() && wanted != azy::theme_key_id(key)) continue;
        const azy::ThemeTokens tokens = azy::theme_tokens(key, appearance.custom_accent);
        const azy::MirrorStyle style =
            azy::make_mirror_style(tokens, appearance, performance_mode, static_cast<float>(dpi_scale));
        std::printf("theme.index %d\n", i);
        std::printf("theme.id %s\n", azy::theme_key_id(key));
        std::printf("theme.name %s\n", azy::theme_key_name(key));
        print_tokens(tokens);
        print_style(style);
        std::printf("--\n");
    }
    return 0;
}
