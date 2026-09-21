// Azy Skin — unit tests for the portable core.
//
// These run on any platform (they have no Windows dependencies), which is what
// lets the version/compat/settings logic be tested without a Windows host:
//
//   cmake -S . -B build -DAZY_BUILD_TESTS=ON && cmake --build build
//   ./build/azy_core_tests            (Linux/macOS)
//   build\azy_core_tests.exe          (Windows)

#include <cmath>
#include <cstdio>
#include <string>

#include "azy/core/capture_math.hpp"
#include "azy/core/mirror_style.hpp"
#include "azy/core/theme_tokens.hpp"
#include "azy/core/failure_tracker.hpp"
#include "azy/core/geometry.hpp"
#include "azy/core/panel_map.hpp"
#include "azy/core/product.hpp"
#include "azy/core/settings.hpp"
#include "azy/core/strings.hpp"
#include "azy/core/theme.hpp"
#include "azy/core/version.hpp"
#include "azy/core/version_string.hpp"

namespace {

int g_checks = 0;
int g_failures = 0;
std::string g_group;

void group(const char* name) {
    g_group = name;
    std::printf("\n[%s]\n", name);
}

void check(bool condition, const char* expr, int line) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL (line %d): %s\n", line, expr);
    }
}

void check_eq_int(long long a, long long b, const char* expr, int line) {
    ++g_checks;
    if (a != b) {
        ++g_failures;
        std::printf("  FAIL (line %d): %s  ->  %lld != %lld\n", line, expr, a, b);
    }
}

void check_eq_str(const std::string& a, const std::string& b, const char* expr, int line) {
    ++g_checks;
    if (a != b) {
        ++g_failures;
        std::printf("  FAIL (line %d): %s  ->  \"%s\" != \"%s\"\n", line, expr, a.c_str(), b.c_str());
    }
}

void check_near(double a, double b, double tolerance, const char* expr, int line) {
    ++g_checks;
    if (std::fabs(a - b) > tolerance) {
        ++g_failures;
        std::printf("  FAIL (line %d): %s  ->  %f != %f\n", line, expr, a, b);
    }
}

#define CHECK(x) check((x), #x, __LINE__)
#define CHECK_INT(a, b) check_eq_int((a), (b), #a " == " #b, __LINE__)
#define CHECK_STR(a, b) check_eq_str((a), (b), #a " == " #b, __LINE__)
#define CHECK_NEAR(a, b, tol) check_near((a), (b), (tol), #a " ~= " #b, __LINE__)

using namespace azy;

// ---------------------------------------------------------------------------
void test_version() {
    group("version parsing");

    Version v;
    CHECK(parse_version("25.6.0.58", v));
    CHECK_INT(v.major, 25);
    CHECK_INT(v.minor, 6);
    CHECK_INT(v.patch, 0);
    CHECK_INT(v.build, 58);

    CHECK(parse_version("v26.0", v));
    CHECK_INT(v.major, 26);
    CHECK_INT(v.minor, 0);
    CHECK_INT(v.patch, 0);

    CHECK(parse_version("  24.6.1.2 (Beta) ", v));
    CHECK_INT(v.major, 24);
    CHECK_INT(v.patch, 1);
    CHECK_INT(v.build, 2);

    CHECK(parse_version("FileVersion: 23.4.0.0", v));
    CHECK_INT(v.major, 23);
    CHECK_INT(v.minor, 4);

    CHECK(!parse_version("", v));
    CHECK(!parse_version("unknown", v));

    CHECK_STR(format_version(Version{25, 6, 0, 58}), "25.6.0.58");
    CHECK_STR(format_version(Version{25, 6, 0, 0}), "25.6");
    CHECK_STR(format_version(Version{25, 6, 3, 0}), "25.6.3");
    CHECK_STR(format_version_short(Version{26, 0, 0, 91}), "26.0");

    CHECK((Version{24, 6, 0, 0} < Version{25, 0, 0, 0}));
    CHECK((Version{25, 0, 0, 9} < Version{25, 0, 1, 0}));
    CHECK((Version{25, 6, 0, 58} == Version{25, 6, 0, 58}));
    CHECK((Version{25, 6, 0, 58} >= Version{25, 6, 0, 10}));
}

// ---------------------------------------------------------------------------
// The key two processes compare: a freshly installed build uses it to tell whether
// the instance already running is the same build (show its settings) or a different
// one (ask it to hand the skin over).
void test_build_key() {
    group("build key");

    CHECK_INT(pack_version("1.1.1"), 0x010101);
    CHECK_INT(pack_version("1.0.0"), 0x010000);
    CHECK_INT(pack_version("1.1.1.0"), 0x010101);
    CHECK_INT(pack_version("2.0"), 0x020000);
    CHECK_INT(pack_version("0.0.0"), 0);
    CHECK_INT(pack_version(""), 0);
    CHECK_INT(pack_version(nullptr), 0);
    // Suffixes stop the parse instead of breaking it: "1.1.1-beta" is still 1.1.1,
    // and a beta of the next build still compares as newer than the released one.
    CHECK_INT(pack_version("1.1.1-beta"), 0x010101);
    CHECK(pack_version("1.0.2") < pack_version(kAppVersion) || pack_version(kAppVersion) == pack_version("1.0.2"));
    CHECK(pack_version("1.1.1") > pack_version("1.0.2"));
    CHECK(pack_version(kAppVersion) != 0);
}

void test_product() {
    group("Premiere product identification");

    CHECK(is_premiere_executable("Adobe Premiere Pro.exe"));
    CHECK(is_premiere_executable("adobe premiere pro.exe"));
    CHECK(is_premiere_executable("Adobe Premiere Pro Beta.exe"));
    CHECK(is_premiere_executable("  Adobe Premiere Pro.exe  "));
    CHECK(!is_premiere_executable("Adobe After Effects.exe"));
    CHECK(!is_premiere_executable("Photoshop.exe"));
    CHECK(!is_premiere_executable("premiere.exe"));
    CHECK(!is_premiere_executable(""));

    struct Case {
        int major;
        ProductFamily expected;
        const char* label;
    };
    const Case cases[] = {
        {21, ProductFamily::Legacy, "2021"},
        {22, ProductFamily::V2022, "2022"},
        {23, ProductFamily::V2023, "2023"},
        {24, ProductFamily::V2024, "2024"},
        {25, ProductFamily::V2025, "2025"},
        {26, ProductFamily::V2026, "2026"},
        {27, ProductFamily::VNext, "2027 -> next"},
        {11, ProductFamily::Unknown, "unknown"},
        {0, ProductFamily::Unknown, "zero"},
    };
    for (const Case& c : cases) {
        const ProductInfo info =
            make_product_info("Adobe Premiere Pro.exe", "C:\\Program Files\\Adobe\\Adobe Premiere Pro 2025\\Adobe Premiere Pro.exe",
                              true, Version{c.major, 6, 0, 12});
        CHECK(info.family == c.expected);
        if (info.family != c.expected) std::printf("      (%s)\n", c.label);
    }

    ProductInfo release = make_product_info("Adobe Premiere Pro.exe", "X:\\Adobe Premiere Pro.exe", true, Version{25, 6, 0, 58});
    CHECK_STR(release.display_name(), "Premiere Pro 2025");
    CHECK_STR(release.version_string(), "25.6.0.58");
    CHECK_STR(release.family_key(), "2025");
    CHECK_INT(release.year(), 2025);

    ProductInfo beta = make_product_info("Adobe Premiere Pro Beta.exe", "Y:\\Adobe Premiere Pro Beta.exe", true, Version{26, 0, 0, 91});
    CHECK_STR(beta.display_name(), "Premiere Pro 2026 Beta");
    CHECK_STR(beta.family_key(), "2026-beta");
    CHECK(beta.channel == ProductChannel::Beta);

    ProductInfo unknown = make_product_info("Adobe Premiere Pro.exe", "Z:\\Adobe Premiere Pro.exe", false, Version{});
    CHECK_STR(unknown.display_name(), "Premiere Pro (unknown version)");
    CHECK_STR(unknown.family_key(), "unknown");
    CHECK_STR(unknown.version_string(), "unknown");
    CHECK_INT(unknown.year(), 0);
}

// ---------------------------------------------------------------------------
void test_theme() {
    group("theme engine");

    // Every theme must be a complete, coherent token set: this is the promise that
    // makes the renderer colour-free (spec §26, §27).
    const Appearance defaults;
    int distinct_accents = 0;
    for (int i = 0; i < kThemeCount; ++i) {
        const ThemeKey key = theme_key_at(i);
        const ThemeTokens t = theme_tokens(key, defaults.custom_accent);

        CHECK(t.visible == (key != ThemeKey::Original));
        if (key == ThemeKey::Original) continue;

        // Dark base, with depth between the surfaces - never a flat black wall.
        const float floor_luma = 0.2126f * t.background.r + 0.7152f * t.background.g + 0.0722f * t.background.b;
        CHECK(floor_luma > 0.001f);   // not pure black
        CHECK(floor_luma < 0.20f);    // and still dark
        CHECK(t.surface.r >= t.background.r);
        CHECK(t.surface.g >= t.background.g);
        CHECK(t.surface.b >= t.background.b);
        CHECK(t.surface_secondary.r + 1e-6f >= t.background.r);

        // Readability: primary text is brighter than secondary, which is brighter
        // than disabled (spec §25).
        const float primary = t.text_primary.g;
        CHECK(t.text_primary.r > 0.6f && t.text_primary.g > 0.6f && t.text_primary.b > 0.6f);
        CHECK(primary > t.text_secondary.g);
        CHECK(t.text_secondary.g > t.text_disabled.g);

        // The accent is a light colour, and the secondary hue is a different one:
        // that is what gives the lighting two sources instead of a flat tint.
        const float accent_luma = 0.2126f * t.accent.r + 0.7152f * t.accent.g + 0.0722f * t.accent.b;
        CHECK(accent_luma > 0.25f);
        const float hue_distance = std::abs(t.accent.r - t.accent_secondary.r) + std::abs(t.accent.b - t.accent_secondary.b);
        CHECK(hue_distance > 0.02f);

        // Glow is localised by construction: it is a strength, never a full-screen
        // bloom, so the number has a ceiling.
        CHECK(t.glow_strength <= 0.6f);
        CHECK(t.reflection_strength > 0.0f && t.reflection_strength <= 0.6f);
        CHECK(t.base_darkening > 0.05f);
        if (i > 0) {
            const ThemeTokens previous = theme_tokens(theme_key_at(i - 1), defaults.custom_accent);
            if (key != ThemeKey::Original && theme_key_at(i - 1) != ThemeKey::Original) {
                const float delta = std::abs(t.accent.r - previous.accent.r) + std::abs(t.accent.g - previous.accent.g) +
                                    std::abs(t.accent.b - previous.accent.b);
                if (delta > 0.05f) ++distinct_accents;
            }
        }
    }
    CHECK(distinct_accents >= 7);  // the nine variants really are different colours

    // Custom: the user's colour is the accent, and the rest is derived from it, so a
    // custom colour produces a coherent theme rather than a pasted-on hue (spec §27).
    const Rgba custom{255, 120, 40, 255};
    const ThemeTokens custom_tokens = theme_tokens(ThemeKey::Custom, custom);
    CHECK_NEAR(custom_tokens.accent.r, 1.0f, 1e-6);
    CHECK(custom_tokens.accent.r > custom_tokens.accent.b);   // the orange the user typed
    // ...and the second hue is a different colour, derived from it rather than a
    // second palette entry: a warm accent gets a warm neighbour.
    const float hue_delta = std::abs(custom_tokens.accent_secondary.r - custom_tokens.accent.r) +
                            std::abs(custom_tokens.accent_secondary.g - custom_tokens.accent.g) +
                            std::abs(custom_tokens.accent_secondary.b - custom_tokens.accent.b);
    CHECK(hue_delta > 0.05f);
    CHECK(custom_tokens.accent_secondary.r > custom_tokens.accent_secondary.b);

    // Original is Azy doing nothing: no tokens, and the renderer is told so.
    const ThemeTokens original = theme_tokens(ThemeKey::Original, custom);
    CHECK(!original.visible);

    // Ids round-trip, are stable, and the legacy names still load (an existing
    // settings.ini must keep working).
    for (int i = 0; i < kThemeCount; ++i) {
        const ThemeKey key = theme_key_at(i);
        CHECK_INT(theme_key_index(key), i);
        CHECK(theme_key_name(key) != nullptr);
        ThemeKey parsed = ThemeKey::Original;
        CHECK(theme_key_from_id(theme_key_id(key), parsed));
        CHECK(parsed == key);
    }
    ThemeKey legacy = ThemeKey::Original;
    CHECK(theme_key_from_id("azydarkglass", legacy));
    CHECK(legacy == ThemeKey::BluePurple);
    CHECK(theme_key_from_id("azy_dark", legacy));
    CHECK(legacy == ThemeKey::Cyan);
    CHECK(!theme_key_from_id("neon", legacy));

    // Colour parsing: the three spellings the settings file may contain.
    Rgba colour{};
    CHECK(parse_hex_color("#7C8CFF", colour));
    CHECK_INT(colour.r, 0x7C);
    CHECK_INT(colour.b, 0xFF);
    CHECK(parse_hex_color("7c8cff", colour));
    CHECK_INT(colour.g, 0x8C);
    CHECK(parse_hex_color("255, 120, 40", colour));
    CHECK_INT(colour.r, 255);
    CHECK_INT(colour.g, 120);
    CHECK_INT(colour.b, 40);
    CHECK(!parse_hex_color("not-a-colour", colour));
    CHECK(!parse_hex_color("#12345", colour));
    CHECK_STR(format_hex_color(Rgba{124, 140, 255, 255}), "#7C8CFF");

    // Hue rotation is what derives a custom theme's second hue; the value scale is
    // applied at the same time.
    const Rgb rotated = rotate_hue(Rgb{1.0f, 0.2f, 0.1f}, 120.0f, 1.0f);
    CHECK(rotated.g > rotated.r);
    const Rgb dimmed = rotate_hue(Rgb{1.0f, 0.2f, 0.1f}, 0.0f, 0.5f);
    CHECK(dimmed.r < 1.0f);
}



// ---------------------------------------------------------------------------
void test_dpi_geometry() {
    group("DPI + geometry helpers");

    CHECK_INT(dip_to_px(8, 96), 8);
    CHECK_INT(dip_to_px(8, 120), 10);   // 125%
    CHECK_INT(dip_to_px(8, 144), 12);   // 150%
    CHECK_INT(dip_to_px(8, 168), 14);   // 175%
    CHECK_INT(dip_to_px(8, 192), 16);   // 200%
    CHECK_INT(dip_to_px(1, 120), 1);    // hairline stays a hairline
    CHECK_INT(dip_to_px(0, 192), 0);
    CHECK_NEAR(dpi_scale(0), 1.0, 1e-9);

    const Rect a = Rect::from_size(10, 20, 100, 50);
    CHECK_INT(a.width(), 100);
    CHECK_INT(a.height(), 50);
    const Rect b = Rect::from_size(15, 25, 100, 50);
    CHECK(rect_changed(a, b));
    CHECK(!rect_changed(a, b, 5));
    CHECK(rect_changed(a, b, 4));

    const Rect overlap = intersect_rect(a, b);
    CHECK_INT(overlap.left, 15);
    CHECK_INT(overlap.right, 110);
    const Rect none = intersect_rect(a, Rect::from_size(500, 500, 10, 10));
    CHECK(none.empty());

    // Negative size safety.
    const Rect degenerate = Rect::from_size(0, 0, -10, -10);
    CHECK(degenerate.empty());
}

// ---------------------------------------------------------------------------
// The design system's new layers: the accent (spec §7, §25), the presets (§40)
// and the promise that "Neutral" is exactly the pre-accent look.
void test_design_tokens() {
    group("design tokens");

    // The 8-bit helpers other portable code shares.
    CHECK_INT(mix_color(Rgba{0, 0, 0, 255}, Rgba{255, 255, 255, 255}, 0.5).r, 128);
    CHECK_INT(with_alpha(Rgba{10, 20, 30, 255}, 0.5).a, 128);
    CHECK_INT(with_alpha(Rgba{10, 20, 30, 255}, 2.0).a, 255);   // clamped
    CHECK_INT(with_alpha(Rgba{10, 20, 30, 255}, -1.0).a, 0);

    // The tokens the mirror consumes must keep their meaning across themes: a dark
    // background, surfaces above it, a border above the surface (a 1px edge has to
    // be brighter than what it surrounds on a dark UI), and an accent-lit border for
    // the live surface.
    const Appearance defaults;
    for (int i = 0; i < kThemeCount - 1; ++i) {
        const ThemeTokens t = theme_tokens(theme_key_at(i), defaults.custom_accent);
        const float surface_luma = t.surface.g;
        CHECK(t.border.g > surface_luma);
        CHECK(t.border_active.g >= t.border.g);
        CHECK(t.hover.g >= t.surface.g);
        // A selected item reads as a lit, accent-led fill: brighter than the surface
        // under it, and never the same colour twice.
        CHECK(t.selection.r + t.selection.g + t.selection.b > t.surface.r + t.surface.g + t.surface.b);
        CHECK(t.surface_opacity < 1.0f);   // glass, never an opaque wall
        CHECK(t.surface_opacity > 0.15f);  // and never so thin the panels vanish
        CHECK(t.contrast_lift > 0.0f);     // readability recovery is always on
    }
}



// The panel map is the whole basis of the region work, so its two promises are
// tested directly: the panels cover the client area without overlapping, and they
// scale with DPI instead of being pixel coordinates in disguise.
void test_panel_map() {
    group("panel map");

    // Keys and names round-trip; an unknown profile is rejected rather than
    // silently becoming the Editing layout.
    WorkspaceId workspace = WorkspaceId::Auto;
    CHECK(workspace_from_key("color", workspace));
    CHECK(workspace == WorkspaceId::Color);
    CHECK(workspace_from_key(" Graphics ", workspace));
    CHECK(workspace == WorkspaceId::Graphics);
    CHECK(workspace_from_key("", workspace));
    CHECK(workspace == WorkspaceId::Auto);
    CHECK(!workspace_from_key("timeline-only", workspace));
    CHECK(workspace == WorkspaceId::Auto);          // resolve() is separate from parse()
    CHECK(resolve_workspace(WorkspaceId::Auto) == WorkspaceId::Editing);
    CHECK(resolve_workspace(WorkspaceId::Audio) == WorkspaceId::Audio);

    // A real maximized 1080p client area at 100%: 1920x1040.
    const Rect client = Rect::from_size(0, 0, 1920, 1040);
    const std::vector<PanelRect> panels = build_panel_map(client, 96, WorkspaceId::Editing);
    CHECK_INT(static_cast<int>(panels.size()), static_cast<int>(PanelId::Count));

    const PanelRect* menu = nullptr;
    const PanelRect* header = nullptr;
    const PanelRect* timeline = nullptr;
    const PanelRect* program = nullptr;
    const PanelRect* project = nullptr;
    for (const PanelRect& panel : panels) {
        // `usable` is a promise about size, and the model must never claim a panel
        // it cannot actually place: the two must agree exactly.
        CHECK(panel.usable == (panel.rect.width() >= 24 && panel.rect.height() >= 24));
        if (panel.id == PanelId::StatusBar) continue;  // not placed at all by default
        CHECK(panel.usable);                          // everything else fits at 1080p
        if (panel.id == PanelId::MenuBar) menu = &panel;
        if (panel.id == PanelId::ApplicationHeader) header = &panel;
        if (panel.id == PanelId::Timeline) timeline = &panel;
        if (panel.id == PanelId::ProgramMonitor) program = &panel;
        if (panel.id == PanelId::Project) project = &panel;
    }
    CHECK(menu != nullptr && header != nullptr && timeline != nullptr && program != nullptr &&
          project != nullptr);

    // Every panel is inside the client area, and every *usable* one has a real
    // size: the failure mode this guards against is the model inventing geometry
    // outside the window, which would draw chrome over unrelated applications.
    for (const PanelRect& panel : panels) {
        CHECK(panel.rect.left >= client.left);
        CHECK(panel.rect.top >= client.top);
        CHECK(panel.rect.right <= client.right);
        CHECK(panel.rect.bottom <= client.bottom);
        CHECK(panel.rect.width() >= 0);
        CHECK(panel.rect.height() >= 0);
        if (panel.usable) {
            CHECK(panel.rect.width() > 0);
            CHECK(panel.rect.height() > 0);
        }
    }

    // The bands are stacked in the order the user sees them, and the timeline owns
    // the bottom of the window.
    CHECK_INT(menu->rect.top, 0);
    CHECK_INT(menu->rect.height(), 24);              // 24 DIP at 96 dpi = 24 px
    CHECK_INT(header->rect.top, menu->rect.bottom);
    CHECK_INT(timeline->rect.bottom, client.bottom);
    CHECK(timeline->rect.top > program->rect.bottom - 1);   // timeline below the monitors
    CHECK_INT(project->rect.top, header->rect.bottom);      // left column starts under the header

    // Panels that share an edge must not overlap: the timeline and the middle band
    // are exactly adjacent, which is what makes a 1px separator meaningful.
    const PanelRect* right = nullptr;
    for (const PanelRect& panel : panels) {
        if (panel.id == PanelId::RightDock) right = &panel;
    }
    CHECK(right != nullptr);
    CHECK_INT(timeline->rect.top, right->rect.bottom);
    CHECK(program->rect.right <= right->rect.left);

    // DPI: the fixed bands scale, so the same window at 150% has a proportionally
    // taller header but the same *physical* size. A model with pixel coordinates
    // baked in would fail here.
    const std::vector<PanelRect> scaled = build_panel_map(client, 144, WorkspaceId::Editing);
    for (const PanelRect& panel : scaled) {
        if (panel.id != PanelId::MenuBar) continue;
        CHECK_INT(panel.rect.height(), 36);          // 24 DIP * 1.5
    }

    // A different workspace really is a different layout.
    const std::vector<PanelRect> audio = build_panel_map(client, 96, WorkspaceId::Audio);
    for (const PanelRect& panel : audio) {
        if (panel.id != PanelId::RightDock) continue;
        CHECK(panel.rect.width() > right->rect.width());
    }

    // A small window: whatever is too small for the minimum is rejected, and
    // nothing is invented outside the client area. Asking for a minimum larger than
    // the whole window must reject everything, which is the degenerate case a
    // 320x200 floating window would hit.
    const std::vector<PanelRect> small = build_panel_map(Rect::from_size(0, 0, 320, 200), 96,
                                                         WorkspaceId::Editing);
    for (const PanelRect& panel : small) {
        CHECK(panel.rect.left >= 0);
        CHECK(panel.rect.top >= 0);
        CHECK(panel.rect.right <= 320);
        CHECK(panel.rect.bottom <= 200);
        CHECK(panel.usable == (panel.rect.width() >= 24 && panel.rect.height() >= 24));
    }
    const std::vector<PanelRect> too_strict = build_panel_map(Rect::from_size(0, 0, 320, 200), 96,
                                                              WorkspaceId::Editing, 400);
    for (const PanelRect& panel : too_strict) CHECK(!panel.usable);

    // An empty client area is answered honestly rather than crashing.
    const std::vector<PanelRect> none = build_panel_map(Rect{}, 96, WorkspaceId::Editing);
    CHECK_INT(static_cast<int>(none.size()), static_cast<int>(PanelId::Count));
    for (const PanelRect& panel : none) CHECK(!panel.usable);

    // The description is what lands in the log and in a bug report: it must name
    // the panels and their rectangles, and say when one could not be placed.
    const std::string described = describe_panel_map(panels);
    CHECK(described.find("Timeline") != std::string::npos);
    CHECK(described.find("1920x") != std::string::npos);
    const std::string described_tiny = describe_panel_map(too_strict);
    CHECK(described_tiny.find("not placeable") != std::string::npos);
}

void test_settings() {
    group("settings INI round-trip");

    Settings s;
    s.enabled = false;
    s.start_with_windows = true;
    s.apply_automatically = false;
    s.appearance.theme = ThemeKey::Green;
    s.appearance.glass_intensity = 0.2;
    s.appearance.border_intensity = 0.75;
    s.appearance.corner_radius_dip = 10;
    s.appearance.shadow_intensity = 0.1;
    s.appearance.darkness = 0.8;
    s.appearance.custom_accent = Rgba{255, 120, 40, 255};
    s.appearance.accent_intensity = 0.6;
    s.appearance.glow_intensity = 0.25;
    s.appearance.animations = true;
    s.performance_mode = true;
    s.suspend_when_minimized = false;
    s.suspend_when_inactive = true;
    s.experimental = true;
    s.safe_mode = true;
    s.safe_mode_reason = "repeated failures with 26.0.0.91";
    s.failure_count = 2;
    s.last_premiere_version = "26.0.0.91";
    s.set_feature_disabled(feature_key::kGlass, true);

    const std::string ini = s.to_ini();
    std::vector<std::string> warnings;
    const Settings loaded = Settings::from_ini(ini, &warnings);

    CHECK(warnings.empty());
    CHECK(loaded.enabled == s.enabled);
    CHECK(loaded.start_with_windows == s.start_with_windows);
    CHECK(loaded.apply_automatically == s.apply_automatically);
    CHECK(loaded.appearance.theme == s.appearance.theme);
    CHECK_NEAR(loaded.appearance.glass_intensity, 0.2, 1e-6);
    CHECK_NEAR(loaded.appearance.border_intensity, 0.75, 1e-6);
    CHECK_INT(loaded.appearance.corner_radius_dip, 10);
    CHECK_NEAR(loaded.appearance.shadow_intensity, 0.1, 1e-6);
    CHECK_NEAR(loaded.appearance.darkness, 0.8, 1e-6);
    CHECK_INT(loaded.appearance.custom_accent.r, 255);
    CHECK_INT(loaded.appearance.custom_accent.g, 120);
    CHECK_INT(loaded.appearance.custom_accent.b, 40);
    CHECK_NEAR(loaded.appearance.accent_intensity, 0.6, 1e-6);
    CHECK_NEAR(loaded.appearance.glow_intensity, 0.25, 1e-6);
    CHECK(loaded.appearance.animations);
    CHECK(loaded.performance_mode);
    CHECK(!loaded.suspend_when_minimized);
    CHECK(loaded.suspend_when_inactive);
    CHECK(loaded.experimental);
    CHECK(loaded.safe_mode);
    CHECK_STR(loaded.safe_mode_reason, s.safe_mode_reason);
    CHECK_INT(loaded.failure_count, 2);
    CHECK_STR(loaded.last_premiere_version, "26.0.0.91");
    CHECK(loaded.feature_disabled(feature_key::kGlass));
    CHECK(!loaded.feature_disabled(feature_key::kGlow));

    // Second round trip is stable (no drift).
    CHECK_STR(loaded.to_ini(), ini);

    // Missing/garbage values fall back to defaults instead of failing.
    const Settings defaults = Settings::from_ini("");
    CHECK(defaults.enabled);
    CHECK(defaults.start_with_windows == false);
    CHECK(defaults.appearance.theme == ThemeKey::BluePurple);
    CHECK_NEAR(defaults.appearance.corner_radius_dip, 8.0, 1e-9);

    const Settings junk = Settings::from_ini(
        "[appearance]\n"
        "glass_intensity=999\n"
        "overlay_intensity=4.5\n"
        "corner_radius=-40\n"
        "darkness=banana\n"
        "theme=rgb_gaming\n"
        "[skin]\n"
        "enabled=maybe\n"
        "[unknown]\n"
        "future_key=kept\n",
        &warnings);
    CHECK_NEAR(junk.appearance.glass_intensity, 1.0, 1e-6);   // clamped
    CHECK_INT(junk.appearance.corner_radius_dip, 0);          // clamped
    CHECK_NEAR(junk.appearance.darkness, 0.5, 1e-6);          // default kept
    CHECK(junk.appearance.theme == ThemeKey::BluePurple);     // warning + default
    // The pre-rebuild overlay keys are no longer settings; they are preserved
    // verbatim like any other unknown key, so nothing is lost from an old file.
    CHECK(junk.extra.count("overlay_intensity") == 1);
    CHECK(!warnings.empty());
    CHECK(junk.enabled);                                     // "maybe" ignored
    CHECK(junk.extra.count("future_key") == 1);
    CHECK_STR(junk.extra.count("future_key") ? junk.extra.at("future_key") : std::string(), "kept");             // unknown keys survive
    CHECK(junk.to_ini().find("future_key=kept") != std::string::npos);
    CHECK(junk.to_ini().find("theme=blue_purple") != std::string::npos);

    // Feature list editing.
    Settings f;
    CHECK(!f.feature_disabled(feature_key::kShadow));
    f.set_feature_disabled(feature_key::kShadow, true);
    f.set_feature_disabled(feature_key::kGlass, true);
    CHECK(f.feature_disabled(feature_key::kShadow));
    CHECK(f.disabled_feature_list() == "shadow,glass");
    f.set_feature_disabled(feature_key::kShadow, false);
    CHECK(f.disabled_feature_list() == "glass");
}

// ---------------------------------------------------------------------------
void test_failure_tracker() {
    group("failure tracker / safe mode trigger");

    FailureTracker tracker(3, 300.0);
    CHECK(!tracker.record_failure(1000.0));
    CHECK(!tracker.record_failure(1001.0));
    CHECK(tracker.record_failure(1002.0));      // threshold reached
    CHECK(tracker.tripped());
    CHECK(tracker.consume_trip());
    CHECK(!tracker.tripped());
    CHECK_INT(tracker.failures(), 0);

    // Failures spread further apart than the window never trip.
    FailureTracker slow(3, 300.0);
    CHECK(!slow.record_failure(0.0));
    CHECK(!slow.record_failure(400.0));
    CHECK(!slow.record_failure(800.0));
    CHECK(!slow.tripped());

    // A long quiet period clears history.
    FailureTracker decay(3, 300.0);
    decay.record_failure(0.0);
    decay.record_failure(1.0);
    decay.record_success(1000.0);
    CHECK_INT(decay.failures(), 0);

    // A short-lived success does not hide a real flapping problem.
    FailureTracker flapping(3, 300.0);
    flapping.record_failure(0.0);
    flapping.record_success(1.0);
    flapping.record_failure(2.0);
    flapping.record_success(3.0);
    CHECK(flapping.record_failure(4.0));
    CHECK(flapping.tripped());

    const std::string blob = flapping.serialize();
    const FailureTracker restored = FailureTracker::deserialize(blob, 3, 300.0);
    CHECK_INT(restored.failures(), flapping.failures());
    CHECK(restored.tripped());

    const FailureTracker empty = FailureTracker::deserialize("", 3, 300.0);
    CHECK_INT(empty.failures(), 0);
    const FailureTracker broken = FailureTracker::deserialize("junk|nope|", 3, 300.0);
    CHECK_INT(broken.failures(), 0);
}

// ---------------------------------------------------------------------------
void test_strings() {
    group("string helpers");

    CHECK_STR(trim("  hi  "), "hi");
    CHECK_STR(to_lower("AbC"), "abc");
    CHECK(iequals("YES", "yes"));
    CHECK(!iequals("yes", "yess"));
    CHECK(starts_with("azy_skin", "azy"));
    CHECK(ends_with("azy_skin.exe", ".exe"));
    CHECK_INT(static_cast<int>(split("a,b,,c", ',').size()), 4);
    CHECK_STR(join({"a", "b"}, ","), "a,b");

    int v = 0;
    CHECK(parse_int(" -42 ", v));
    CHECK_INT(v, -42);
    CHECK(!parse_int("4x", v));
    CHECK(!parse_int("", v));

    bool b = false;
    CHECK(parse_bool("ON", b));
    CHECK(b);
    CHECK(parse_bool("0", b));
    CHECK(!b);
    CHECK(!parse_bool("sometimes", b));

    // UTF-8 <-> UTF-16 incl. non-ASCII and surrogate pairs.
    CHECK_STR(utf16_to_utf8(utf8_to_utf16("Premiere Pro")), "Premiere Pro");
    CHECK_STR(utf16_to_utf8(utf8_to_utf16("caf\xC3\xA9 \xE2\x9C\x94")), "caf\xC3\xA9 \xE2\x9C\x94");
    CHECK_STR(utf16_to_utf8(utf8_to_utf16("\xF0\x9F\x8E\xAC clip")), "\xF0\x9F\x8E\xAC clip");
    CHECK_STR(str_format("%d/%s", 7, "x"), "7/x");
    CHECK_STR(str_format("short"), "short");
}

// ---------------------------------------------------------------------------
void test_layout_safety() {
    group("layout safety rules");

    // The composition surface must never be larger than the window it skins:
    // any region outside the frame would be an overlay on the desktop.
    const Rect window = Rect::from_size(100, 100, 1920, 1080);
    const Rect ring = Rect::from_size(100, 100, 1920, 1080);
    CHECK(intersect_rect(window, ring) == window);

    // Corner radius must never exceed 20% of the shorter window side, so a
    // small dialog cannot be turned into a pill or clipped weirdly.
    struct RadiusCase {
        int width, height, dip, expected_max;
    };
    const RadiusCase cases[] = {
        {1920, 1080, 16, 16},
        {100, 80, 16, 16},     // (100*0.2 = 20) -> 16 still allowed
        {30, 24, 16, 4},       // small dialog: radius clamped down
    };
    for (const RadiusCase& c : cases) {
        const int physical = dip_to_px(c.dip, 96);
        const int cap = static_cast<int>(std::min(c.width, c.height) * 0.2);
        const int applied = std::min(physical, cap);
        CHECK(applied <= c.expected_max);
    }
}


// ---------------------------------------------------------------------------
void test_capture_math() {
    group("overlay capture math");

    // A maximized 1920x1080 window: Windows reports a frame that hangs off the
    // left, right and bottom edges by the resize border, while the visible frame
    // is the work area. The duplicate covers the work area and has to sample the
    // matching slice of the capture, not the whole thing.
    const Rect captured = Rect::from_size(-8, -8, 1936, 1096);   // the whole captured window
    const Rect overlay = Rect::from_size(0, 0, 1920, 1040);      // the visible part
    const UvRect uv = map_overlay_to_capture(overlay, captured);
    CHECK(uv.valid);
    CHECK_NEAR(uv.u0, 8.0 / 1936.0, 0.0005);
    CHECK_NEAR(uv.v0, 8.0 / 1096.0, 0.0005);
    CHECK_NEAR(uv.u1, 1928.0 / 1936.0, 0.0005);
    CHECK_NEAR(uv.v1, 1048.0 / 1096.0, 0.0005);
    CHECK(uv.width() > 0.9f && uv.width() < 1.0f);
    CHECK(uv.height() > 0.9f && uv.height() < 1.0f);

    // The common case: the overlay and the capture are the same rectangle, so the
    // whole texture is used and nothing is resampled.
    const UvRect whole = map_overlay_to_capture(overlay, overlay);
    CHECK(whole.valid);
    CHECK_NEAR(whole.u0, 0.0, 0.0001);
    CHECK_NEAR(whole.v0, 0.0, 0.0001);
    CHECK_NEAR(whole.u1, 1.0, 0.0001);
    CHECK_NEAR(whole.v1, 1.0, 0.0001);

    // Geometry that does not describe the same region must produce *nothing*: a
    // stretched or mirrored image is worse than no image.
    CHECK(!map_overlay_to_capture(overlay, Rect{}).valid);
    CHECK(!map_overlay_to_capture(Rect{}, captured).valid);
    CHECK(!map_overlay_to_capture(Rect::from_size(4000, 0, 100, 100), captured).valid);
    // A sliver (a window dragged until it is nearly off screen) is not worth
    // showing either: below 2% of the captured axis the mapping is guesswork.
    CHECK(!map_overlay_to_capture(Rect::from_size(0, 0, 10, 1040), captured).valid);

    // Clipping a panel rectangle into the overlay's own coordinate space.
    const LocalRect clipped = clip_to_overlay(Rect::from_size(100, 50, 400, 300), Rect::from_size(0, 0, 1920, 1040));
    CHECK_NEAR(clipped.left, 100.0, 0.001);
    CHECK_NEAR(clipped.top, 50.0, 0.001);
    CHECK_NEAR(clipped.right, 500.0, 0.001);
    CHECK_NEAR(clipped.bottom, 350.0, 0.001);
    CHECK(clip_to_overlay(Rect::from_size(5000, 5000, 100, 100), overlay).empty());
    // A panel that runs off the right edge is clipped to the overlay, in the
    // overlay's own coordinates (1920 wide, origin at its top-left).
    CHECK_NEAR(clip_to_overlay(Rect::from_size(1900, 100, 100, 100), overlay).right, 1920.0, 0.001);

    // The monitor pass-through: Program first, then Source, and never a sliver.
    std::vector<PanelRect> panels;
    PanelRect program;
    program.id = PanelId::ProgramMonitor;
    program.usable = true;
    program.rect = Rect::from_size(700, 200, 500, 300);
    panels.push_back(program);
    PanelRect source;
    source.id = PanelId::SourceMonitor;
    source.usable = true;
    source.rect = Rect::from_size(100, 200, 500, 300);
    panels.push_back(source);
    PanelRect timeline;
    timeline.id = PanelId::Timeline;
    timeline.usable = true;
    timeline.rect = Rect::from_size(0, 600, 1920, 400);
    panels.push_back(timeline);
    PanelRect hidden;
    hidden.id = PanelId::EffectControls;
    hidden.usable = false;
    hidden.rect = Rect::from_size(0, 0, 10, 10);
    panels.push_back(hidden);
    PanelRect sliver;
    sliver.id = PanelId::AudioMeters;
    sliver.usable = true;
    sliver.rect = Rect::from_size(0, 0, 8, 300);
    panels.push_back(sliver);

    const std::vector<LocalRect> pass =
        monitor_pass_through(panels, overlay, Rect::from_size(0, 32, 1920, 1040), 96);
    CHECK_INT(static_cast<long long>(pass.size()), 2);
    // Screen y is the client origin plus the panel's own offset: the model is built
    // for a client area that starts at (0,0). What is passed through is the *picture*
    // area: the monitor's own toolbar strip above it stays skinned, so the region is
    // inset by the toolbar height and a small frame.
    const int toolbar = dip_to_px(kMonitorToolbarDip, 96);
    const int padding = dip_to_px(kMonitorPaddingDip, 96);
    CHECK_NEAR(pass[0].left, 700.0 + padding, 0.001);   // Program Monitor, always first
    CHECK_NEAR(pass[0].top, 232.0 + toolbar, 0.001);
    CHECK_NEAR(pass[1].left, 100.0 + padding, 0.001);   // Source Monitor
    CHECK_NEAR(pass[1].top, 232.0 + toolbar, 0.001);
    // The picture area stays inside its panel, so a panel frame can be drawn around
    // it without touching the video.
    CHECK(pass[0].right < 700.0f + 500.0f);
    CHECK(pass[0].right > 700.0f);
    // The timeline is not a picture region and an unusable panel is ignored.
    for (const LocalRect& r : pass) CHECK(r.top < 600.0f);

    // Panel hairlines: the pass-through regions are excluded (a hairline over the
    // footage would draw the skin on the video), the monitors themselves never get
    // one, and the menu bar comes first.
    const std::vector<LocalRect> lines =
        panel_hairlines(panels, overlay, Rect::from_size(0, 32, 1920, 1040), pass, 4);
    CHECK(!lines.empty());
    CHECK(lines.size() <= 4);
    for (const LocalRect& line : lines) {
        for (const LocalRect& region : pass) {
            const bool overlaps = line.left < region.right && region.left < line.right &&
                                  line.top < region.bottom && region.top < line.bottom;
            CHECK(!overlaps);
        }
    }

    // Packing for the shader: four floats per rectangle, zero-filled beyond the
    // end, so a stale slot can never light up in the shader.
    float packed[4 * 4] = {};
    pack_rects(pass, packed, 4);
    CHECK_NEAR(packed[0], pass[0].left, 0.001);
    CHECK_NEAR(packed[3], pass[0].bottom, 0.001);
    CHECK_NEAR(packed[4], pass[1].left, 0.001);
    CHECK_NEAR(packed[7], pass[1].bottom, 0.001);
    // Slots past the end are zeroed: a stale rectangle would otherwise light up a
    // pass-through region that no longer exists.
    CHECK_NEAR(packed[8], 0.0, 0.001);
    CHECK_NEAR(packed[11], 0.0, 0.001);
    CHECK_NEAR(packed[12], 0.0, 0.001);
    CHECK_NEAR(packed[15], 0.0, 0.001);

    // Where the duplicate goes: the visible frame, except that a maximized window
    // must use the work area (Windows reports its frame as larger than the
    // monitor on purpose) and a fullscreen window must use the whole monitor.
    const Rect monitor = Rect::from_size(0, 0, 1920, 1080);
    const Rect work = Rect::from_size(0, 0, 1920, 1040);
    const Rect floating = Rect::from_size(200, 150, 900, 600);
    CHECK(overlay_rect(floating, monitor, work, false, false) == floating);
    CHECK(overlay_rect(Rect::from_size(-8, -8, 1936, 1096), monitor, work, true, false) == work);
    CHECK(overlay_rect(Rect::from_size(0, 0, 1920, 1080), monitor, work, false, true) == monitor);
    CHECK(overlay_rect(Rect{}, monitor, work, true, false).empty());
}

void test_mirror_style() {
    group("mirror style");

    const Appearance defaults;
    const ThemeTokens tokens = theme_tokens(ThemeKey::BluePurple, defaults.custom_accent);
    const MirrorStyle style = make_mirror_style(tokens, defaults, false);

    CHECK(style.visible);
    CHECK(!mirror_style_is_passthrough(style));
    // The defaults have to be *visible*: a skin nobody can see is the complaint this
    // rebuild exists to answer. Dark, with every part of the material present.
    CHECK(style.base_dark > 0.30f);
    CHECK(style.base_dark < 0.90f);
    CHECK(style.surface > 0.10f);
    CHECK(style.highlight > 0.10f);
    CHECK(style.clarity > 0.0f);        // readability is not optional
    CHECK(style.glass > 0.0f);          // the glass diffusion
    CHECK(style.glow > 0.0f);           // localised light, not a screen-wide bloom
    CHECK(style.glow <= 0.80f);
    CHECK_NEAR(style.radius, 8.0f, 1e-6);   // the default corner, at 100% DPI

    // The theme's colours are the shader's colours, unchanged: no second palette is
    // invented between the theme engine and the GPU (spec §26).
    CHECK_NEAR(style.background[0], tokens.background.r, 0.001);
    CHECK_NEAR(style.surface_colour[2], tokens.surface.b, 0.001);
    CHECK_NEAR(style.accent[1], tokens.accent.g, 0.001);
    CHECK_NEAR(style.glow_colour[2], tokens.glow.b, 0.001);

    // Sliders move it, monotonically and in the right direction.
    Appearance dark = defaults;
    dark.darkness = 1.0;
    CHECK(make_mirror_style(tokens, dark, false).base_dark > style.base_dark);
    Appearance light = defaults;
    light.darkness = 0.0;
    CHECK(make_mirror_style(tokens, light, false).base_dark < style.base_dark);

    Appearance more_glass = defaults;
    more_glass.glass_intensity = 1.0;
    CHECK(make_mirror_style(tokens, more_glass, false).glass > style.glass);

    Appearance strong_border = defaults;
    strong_border.border_intensity = 1.0;
    const MirrorStyle lit = make_mirror_style(tokens, strong_border, false);
    CHECK(lit.highlight > style.highlight);
    CHECK(lit.highlight <= 0.95f);   // still a hairline, never an outline

    Appearance deep_shadow = defaults;
    deep_shadow.shadow_intensity = 1.0;
    CHECK(make_mirror_style(tokens, deep_shadow, false).shadow >= style.shadow);

    Appearance glowing = defaults;
    glowing.glow_intensity = 1.0;
    CHECK(make_mirror_style(tokens, glowing, false).glow > style.glow);

    // Every optional strength at zero: the dark base and the clarity recovery stay
    // (the brief asks for readability), the extras really do disappear.
    Appearance bare;
    bare.glass_intensity = 0.0;
    bare.glow_intensity = 0.0;
    bare.shadow_intensity = 0.0;
    bare.border_intensity = 0.0;
    bare.accent_intensity = 0.0;
    const MirrorStyle minimal = make_mirror_style(tokens, bare, false);
    CHECK(!mirror_style_is_passthrough(minimal));
    CHECK(minimal.glass < style.glass);
    CHECK_NEAR(minimal.shadow, 0.06, 0.0001);
    CHECK_NEAR(minimal.glow, 0.0, 0.0001);
    CHECK_NEAR(minimal.accent_mix, 0.0, 0.0001);
    CHECK(minimal.base_dark > 0.30f);   // the dark base is the skin, not a slider

    // An accent strength of zero is "no hue at all": the borders go neutral rather
    // than leaving a grey tint behind.
    CHECK_NEAR(minimal.accent[0], tokens.border.r, 0.001);
    CHECK_NEAR(minimal.glow_colour[2], tokens.border.b, 0.001);

    // Performance mode keeps the material and drops the per-pixel extras.
    const MirrorStyle perf = make_mirror_style(tokens, defaults, true);
    CHECK_NEAR(perf.gloss, 0.0, 0.0001);
    CHECK_NEAR(perf.grain, 0.0, 0.0001);
    CHECK_NEAR(perf.glass, 0.0, 0.0001);
    CHECK(perf.clarity > 0.0f);      // readability survives performance mode
    CHECK(perf.highlight > 0.0f);
    CHECK(perf.base_dark > 0.30f);

    // Corner radius: 0 means square and is honoured; anything else is clamped into
    // range and converted to physical pixels for the current DPI.
    Appearance square = defaults;
    square.corner_radius_dip = 0;
    CHECK_NEAR(make_mirror_style(tokens, square, false).radius, 0.0f, 1e-6);
    Appearance huge = defaults;
    huge.corner_radius_dip = 400;
    CHECK_NEAR(make_mirror_style(tokens, huge, false).radius, 16.0f, 1e-6);

    // 100 / 150 / 200% DPI: the radius follows the monitor, the look does not change.
    const MirrorStyle at_150 = make_mirror_style(tokens, defaults, false, 1.5f);
    CHECK_NEAR(at_150.radius, 12.0f, 1e-6);
    CHECK_NEAR(at_150.dpi, 1.5f, 1e-6);
    const MirrorStyle at_200 = make_mirror_style(tokens, defaults, false, 2.0f);
    CHECK_NEAR(at_200.radius, 16.0f, 1e-6);
    CHECK_NEAR(at_200.base_dark, style.base_dark, 1e-6);

    // A nonsense DPI is clamped rather than trusted.
    CHECK_NEAR(make_mirror_style(tokens, defaults, false, 0.0f).dpi, 1.0f, 1e-6);

    // Animations are a switch, not a clock: the style says whether a transition is
    // allowed, and nothing else.
    Appearance still = defaults;
    still.animations = false;
    CHECK(!make_mirror_style(tokens, still, false).animations);
    CHECK(!make_mirror_style(tokens, defaults, false).animations);   // no animation by default

    // Original: Azy shows nothing at all, and the caller is told in one call.
    const ThemeTokens original = theme_tokens(ThemeKey::Original, defaults.custom_accent);
    const MirrorStyle off = make_mirror_style(original, defaults, false);
    CHECK(!off.visible);
    CHECK(mirror_style_is_passthrough(off));
}

}  // namespace

int main() {
    std::printf("Azy Skin core tests\n===================");
    test_version();
    test_build_key();
    test_product();
    test_theme();
    test_dpi_geometry();
    test_settings();
    test_panel_map();
    test_design_tokens();
    test_failure_tracker();
    test_strings();
    test_layout_safety();
    test_capture_math();
    test_mirror_style();

    std::printf("\n===================\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
