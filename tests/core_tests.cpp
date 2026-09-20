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

#include "azy/core/compat.hpp"
#include "azy/core/failure_tracker.hpp"
#include "azy/core/geometry.hpp"
#include "azy/core/product.hpp"
#include "azy/core/ring_layout.hpp"
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
HostCapabilities win11_host() {
    HostCapabilities h;
    h.windows_build = 22631;
    h.dwm_composition = true;
    h.dark_titlebar = true;
    h.frame_colors = true;
    h.rounded_corners = true;
    h.system_backdrop = true;
    return h;
}

HostCapabilities win10_1809_host() {
    HostCapabilities h;
    h.windows_build = 17763;
    h.dwm_composition = true;
    h.dark_titlebar = true;
    h.frame_colors = false;
    h.rounded_corners = false;
    h.system_backdrop = false;
    return h;
}

void test_compat() {
    group("version-aware compatibility policy");

    const ProductInfo p2025 = make_product_info("Adobe Premiere Pro.exe", "p.exe", true, Version{25, 6, 0, 58});
    const ProductInfo p2024 = make_product_info("Adobe Premiere Pro.exe", "p.exe", true, Version{24, 6, 1, 2});
    const ProductInfo p2026 = make_product_info("Adobe Premiere Pro.exe", "p.exe", true, Version{26, 0, 0, 91});
    const ProductInfo future = make_product_info("Adobe Premiere Pro.exe", "p.exe", true, Version{27, 1, 0, 0});
    const ProductInfo unknown = make_product_info("Adobe Premiere Pro.exe", "p.exe", false, Version{});
    const ProductInfo legacy = make_product_info("Adobe Premiere Pro.exe", "p.exe", true, Version{20, 0, 0, 0});
    const ProductInfo beta = make_product_info("Adobe Premiere Pro Beta.exe", "p.exe", true, Version{26, 0, 0, 5});

    FeatureSet full = resolve_features(p2025, win11_host(), false, false, false);
    CHECK(full.dark_frame);
    CHECK(full.window_overlay);  // the whole-window overlay is on by default
    CHECK(full.frame_colors);
    CHECK(full.rounded_frame);
    CHECK(full.edge_surface);
    CHECK(full.edge_surface_rounded);
    CHECK(!full.frame_backdrop);  // experimental: never on by default
    CHECK_STR(feature_summary(full), "full");

    // Without layered windows there is no overlay and no ring: both are Azy's own
    // windows, and neither can be drawn without them.
    HostCapabilities no_layered = win11_host();
    no_layered.layered_windows = false;
    const FeatureSet plain = resolve_features(p2025, no_layered, false, false, false);
    CHECK(!plain.window_overlay);
    CHECK(!plain.edge_surface);

    FeatureSet v24 = resolve_features(p2024, win11_host(), false, false, false);
    CHECK(v24.frame_colors);
    CHECK(v24.edge_surface);

    FeatureSet v26 = resolve_features(p2026, win11_host(), false, false, false);
    CHECK(v26.frame_colors);
    CHECK(v26.rounded_frame);

    FeatureSet fut = resolve_features(future, win11_host(), false, false, false);
    CHECK(fut.dark_frame);
    CHECK(!fut.frame_colors);   // newer than known -> conservative
    CHECK(!fut.rounded_frame);
    CHECK(fut.edge_surface);    // still safe: our own hairline is inert

    FeatureSet unk = resolve_features(unknown, win11_host(), false, false, false);
    CHECK(!unk.frame_colors);
    CHECK(!unk.rounded_frame);
    CHECK(!unk.frame_backdrop);
    CHECK(unk.edge_surface);
    CHECK(!unk.reasons.empty());

    FeatureSet old_os = resolve_features(p2025, win10_1809_host(), false, false, false);
    CHECK(old_os.dark_frame);
    CHECK(!old_os.frame_colors);
    CHECK(!old_os.rounded_frame);

    FeatureSet perf = resolve_features(p2025, win11_host(), false, /*performance_mode=*/true, false);
    CHECK(!perf.frame_backdrop);
    CHECK(!perf.edge_surface_rounded);

    FeatureSet safe = resolve_features(p2025, win11_host(), /*explicit_safe_mode=*/true, false, false);
    CHECK(safe.dark_frame);
    CHECK(!safe.frame_colors);
    CHECK(!safe.edge_surface);
    CHECK(!safe.rounded_frame);
    CHECK_STR(feature_summary(safe), "safe mode");

    FeatureSet experimental = resolve_features(p2026, win11_host(), false, false, /*experimental_opt_in=*/true);
    CHECK(experimental.frame_backdrop);

    FeatureSet legacy_fs = resolve_features(legacy, win11_host(), false, false, false);
    CHECK(!legacy_fs.frame_colors);
    CHECK(!legacy_fs.rounded_frame);
    CHECK(legacy_fs.edge_surface);

    FeatureSet beta_fs = resolve_features(beta, win11_host(), false, false, false);
    CHECK(beta_fs.frame_colors);
    CHECK(!beta_fs.rounded_frame);

    // Experimental opt-in must not resurrect features on an unknown build.
    FeatureSet unknown_exp = resolve_features(unknown, win11_host(), false, false, true);
    CHECK(!unknown_exp.frame_backdrop);
    CHECK(!unknown_exp.frame_colors);
}

// ---------------------------------------------------------------------------
void test_theme() {
    group("theme palette");

    Appearance appearance;  // defaults

    const ThemePalette glass = make_palette(ThemeId::AzyDarkGlass, appearance, true);
    CHECK(glass.visible);
    CHECK(glass.draw_surface);
    CHECK(glass.apply_frame_colors);
    CHECK(glass.surface_fill.a < 255);   // subtle translucency
    CHECK(glass.surface_fill.a > 180);   // ...but panels stay readable
    CHECK(glass.surface_border.a < 40);  // hairline, not an outline
    CHECK(glass.surface_border.a > 0);
    CHECK(glass.surface_shadow.a <= 90);
    CHECK_INT(glass.corner_radius_dip, 8);

    // The raised bezel is what makes the frame edge readable on a dark UI: it must
    // be visible, but still an edge treatment rather than a border line, and
    // lighter than the surface it sits on (a darker edge would vanish against
    // Premiere's own near-black panels).
    CHECK(glass.surface_bezel.a > glass.surface_border.a);
    CHECK(glass.surface_bezel.a < 90);
    CHECK(glass.surface_bezel.r > glass.surface_fill.r);
    CHECK(glass.surface_bezel.r <= glass.surface_border.r);

    // The whole-window overlay: its alpha is the strength, a zero alpha means
    // "off", and it stays a tint rather than an opaque sheet even at 100%.
    CHECK(glass.surface_veil.a > 0);
    CHECK(glass.surface_veil.a < 200);
    Appearance overlay_max = appearance;
    overlay_max.overlay_intensity = 1.0;
    const ThemePalette veil_max = make_palette(ThemeId::AzyDarkGlass, overlay_max, true);
    CHECK(veil_max.surface_veil.a > glass.surface_veil.a);
    CHECK(veil_max.surface_veil.a <= 200);
    Appearance no_overlay = appearance;
    no_overlay.overlay = false;
    CHECK_INT(make_palette(ThemeId::AzyDarkGlass, no_overlay, true).surface_veil.a, 0);
    Appearance zero_overlay = appearance;
    zero_overlay.overlay_intensity = 0.0;
    CHECK_INT(make_palette(ThemeId::AzyDarkGlass, zero_overlay, true).surface_veil.a, 0);

    // More glass -> more transparency, monotonically.
    Appearance more = appearance;
    more.glass_intensity = 1.0;
    const ThemePalette glass_more = make_palette(ThemeId::AzyDarkGlass, more, true);
    CHECK(glass_more.surface_fill.a < glass.surface_fill.a);

    Appearance no_glass = appearance;
    no_glass.glass_intensity = 0.0;
    const ThemePalette glass_none = make_palette(ThemeId::AzyDarkGlass, no_glass, true);
    CHECK(glass_none.surface_fill.a >= 235);   // "no glass" is still not fully opaque...
    CHECK(glass_none.surface_fill.a <= 245);   // ...but close to it (subtle by design)

    // Stronger borders raise alpha but stay subtle.
    Appearance strong = appearance;
    strong.border_intensity = 1.0;
    const ThemePalette strong_border = make_palette(ThemeId::AzyDarkGlass, strong, true);
    CHECK(strong_border.surface_border.a > glass.surface_border.a);
    CHECK(strong_border.surface_border.a <= 40);
    CHECK(strong_border.surface_bezel.a > glass.surface_bezel.a);
    CHECK(strong_border.surface_bezel.a <= 45);  // 0.05 + 0.12 -> 17% white, never a bright outline

    // Opaque theme: no translucency at all.
    const ThemePalette dark = make_palette(ThemeId::AzyDark, appearance, true);
    CHECK_INT(dark.surface_fill.a, 255);
    CHECK(dark.surface_bezel.a > 0);  // the bezel is the visual anchor, in both themes

    // Original: Azy does nothing.
    const ThemePalette original = make_palette(ThemeId::Original, appearance, true);
    CHECK(!original.visible);
    CHECK(!original.draw_surface);
    CHECK(!original.apply_frame_colors);
    CHECK_INT(original.surface_fill.a, 0);
    CHECK_INT(original.surface_veil.a, 0);  // "Original" leaves the window alone too
    CHECK_INT(original.surface_bezel.a, 0);
    CHECK_INT(original.surface_border.a, 0);
    CHECK_INT(original.corner_radius_dip, 0);

    // Frame colours are only requested when the host supports them.
    const ThemePalette no_support = make_palette(ThemeId::AzyDarkGlass, appearance, false);
    CHECK(!no_support.apply_frame_colors);
    CHECK(no_support.draw_surface);

    // Darkness must stay in a comfortable range, never pure black.
    Appearance darkest = appearance;
    darkest.darkness = 1.0;
    const ThemePalette darkest_palette = make_palette(ThemeId::AzyDarkGlass, darkest, true);
    const Rgba fill = darkest_palette.surface_fill;
    CHECK(fill.r > 8);
    CHECK(fill.g > 8);
    CHECK(fill.b > 8);

    // Radius clamping is enforced by the palette too.
    Appearance silly = appearance;
    silly.corner_radius_dip = 400;
    CHECK_INT(make_palette(ThemeId::AzyDarkGlass, silly, true).corner_radius_dip, 16);

    CHECK_STR(theme_name(ThemeId::AzyDarkGlass), "Azy Dark Glass");
    ThemeId parsed = ThemeId::Original;
    CHECK(theme_from_key("azy_dark", parsed));
    CHECK(parsed == ThemeId::AzyDark);
    CHECK(!theme_from_key("neon", parsed));
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

    // Accent keys round-trip and are tolerant of the spellings a hand-edited file
    // might contain.
    AccentId accent = AccentId::Neutral;
    CHECK(accent_from_key("blue_violet", accent));
    CHECK(accent == AccentId::BlueViolet);
    CHECK(accent_from_key(" Violet ", accent));
    CHECK(accent == AccentId::Violet);
    CHECK(accent_from_key("PURPLE", accent));
    CHECK(accent == AccentId::Violet);
    CHECK(accent_from_key("", accent));
    CHECK(accent == AccentId::Neutral);   // an empty value means "no hue"
    CHECK(!accent_from_key("neon-green", accent));
    CHECK_STR(accent_key(AccentId::Neutral), "neutral");

    // Every accent is distinguishable, and the neutral one is white.
    const Rgba blue = accent_color(AccentId::Blue);
    const Rgba violet = accent_color(AccentId::Violet);
    const Rgba blue_violet = accent_color(AccentId::BlueViolet);
    CHECK(blue.r != violet.r);
    CHECK(blue_violet.b > blue_violet.r);          // a cool hue, as the reference's lighting is
    const Rgba neutral = accent_color(AccentId::Neutral);
    CHECK_INT(neutral.r, 255);
    CHECK_INT(neutral.b, 255);

    Appearance appearance;  // defaults: blue-violet at 35%
    const ThemePalette lit = make_palette(ThemeId::AzyDarkGlass, appearance, true);
    CHECK_NEAR(lit.accent_strength, 0.35, 1e-9);
    CHECK(lit.accent.b > lit.accent.r);            // blue-violet, not warm
    CHECK(lit.surface_bezel.b > lit.surface_bezel.r);  // ...and it reaches the hairline

    // Neutral reproduces the pre-accent hairline exactly: same weights, white hue.
    Appearance plain = appearance;
    plain.accent = AccentId::Neutral;
    const ThemePalette neutral_palette = make_palette(ThemeId::AzyDarkGlass, plain, true);
    CHECK_NEAR(neutral_palette.accent_strength, 0.0, 1e-9);
    Appearance legacy = appearance;
    legacy.accent = AccentId::Neutral;
    legacy.accent_intensity = 0.0;
    const ThemePalette legacy_palette = make_palette(ThemeId::AzyDarkGlass, legacy, true);
    CHECK_INT(neutral_palette.surface_bezel.r, legacy_palette.surface_bezel.r);
    CHECK_INT(neutral_palette.surface_border.a, legacy_palette.surface_border.a);
    CHECK_INT(neutral_palette.surface_veil.b, legacy_palette.surface_veil.b);

    // The overlay carries a hint of the accent, never enough to look like a filter:
    // a few points more blue than the charcoal's own deliberate blue tint, and far
    // below anything that would read as "a blue window".
    const int lit_tint = lit.surface_veil.b - lit.surface_veil.r;
    const int neutral_tint = neutral_palette.surface_veil.b - neutral_palette.surface_veil.r;
    CHECK(neutral_tint > 0);       // the base charcoal is blue-tinted by design
    CHECK(neutral_tint <= 4);
    CHECK(lit_tint >= neutral_tint);
    CHECK(lit_tint <= 12);

    // Glow raises the edge alpha only when it is asked for; zero by default.
    CHECK_NEAR(lit.glow, 0.0, 1e-9);
    Appearance glowing = appearance;
    glowing.glow_intensity = 1.0;
    const ThemePalette glow = make_palette(ThemeId::AzyDarkGlass, glowing, true);
    CHECK(glow.surface_bezel.a > lit.surface_bezel.a);
    CHECK(glow.surface_border.a > lit.surface_border.a);

    // Presets: the four are distinct, Balanced is the default look, and the two
    // cheap ones switch Performance mode on.
    const PresetValues ultra = preset_values(PresetId::Ultra);
    const PresetValues balanced = preset_values(PresetId::Balanced);
    const PresetValues perf = preset_values(PresetId::Performance);
    const PresetValues low = preset_values(PresetId::LowPower);
    CHECK(ultra.glass_intensity > balanced.glass_intensity);
    CHECK(ultra.overlay_intensity > balanced.overlay_intensity);
    CHECK(ultra.glow_intensity > 0.0);            // Ultra is the only preset with glow
    CHECK(balanced.glow_intensity == 0.0);
    CHECK(balanced.animations == false);          // static by default, always
    CHECK(!balanced.performance_mode);
    CHECK(perf.performance_mode);
    CHECK(low.performance_mode);
    CHECK(perf.corner_radius_dip == 0);           // no rounding when it is about cost
    CHECK(low.overlay_intensity < balanced.overlay_intensity);
    Appearance defaults;                          // the shipped defaults ARE Balanced
    CHECK_NEAR(balanced.glass_intensity, defaults.glass_intensity, 1e-9);
    CHECK_NEAR(balanced.border_intensity, defaults.border_intensity, 1e-9);
    CHECK_NEAR(balanced.darkness, defaults.darkness, 1e-9);
    CHECK(balanced.corner_radius_dip == defaults.corner_radius_dip);
    CHECK_NEAR(balanced.overlay_intensity, defaults.overlay_intensity, 1e-9);
    CHECK(balanced.overlay == defaults.overlay);

    // Applying a preset lands exactly on that preset, and moving one slider after
    // that reports Custom instead of claiming the preset is still in effect.
    Settings settings;
    CHECK(settings.current_preset() == PresetId::Balanced);  // defaults are Balanced
    settings.apply_preset(PresetId::Ultra);
    CHECK(settings.current_preset() == PresetId::Ultra);
    CHECK(settings.appearance.animations);
    CHECK(settings.appearance.theme == ThemeId::AzyDarkGlass);  // a preset is not a theme
    settings.apply_preset(PresetId::LowPower);
    CHECK(settings.current_preset() == PresetId::LowPower);
    CHECK(settings.performance_mode);
    settings.appearance.border_intensity = 0.99;
    CHECK(settings.current_preset() == PresetId::Custom);
    // ...and Custom applied by hand means "go back to the baseline".
    settings.apply_preset(PresetId::Custom);
    CHECK(settings.current_preset() == PresetId::Balanced);

    // Preset keys round-trip through the INI.
    PresetId preset = PresetId::Custom;
    CHECK(preset_from_key("low_power", preset));
    CHECK(preset == PresetId::LowPower);
    CHECK(preset_from_key("default", preset));
    CHECK(preset == PresetId::Balanced);
    CHECK(!preset_from_key("ultra-max", preset));
}

void test_settings() {
    group("settings INI round-trip");

    Settings s;
    s.enabled = false;
    s.start_with_windows = true;
    s.apply_automatically = false;
    s.appearance.theme = ThemeId::AzyDark;
    s.appearance.glass_intensity = 0.2;
    s.appearance.border_intensity = 0.75;
    s.appearance.corner_radius_dip = 10;
    s.appearance.shadow_intensity = 0.1;
    s.appearance.darkness = 0.8;
    s.appearance.overlay = false;
    s.appearance.overlay_intensity = 0.15;
    s.appearance.accent = AccentId::Violet;
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
    s.set_feature_disabled(feature_key::kFrameBackdrop, true);

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
    CHECK(!loaded.appearance.overlay);
    CHECK_NEAR(loaded.appearance.overlay_intensity, 0.15, 1e-6);
    CHECK(loaded.appearance.accent == AccentId::Violet);
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
    CHECK(loaded.feature_disabled(feature_key::kFrameBackdrop));
    CHECK(!loaded.feature_disabled(feature_key::kEdgeSurface));

    // Second round trip is stable (no drift).
    CHECK_STR(loaded.to_ini(), ini);

    // Missing/garbage values fall back to defaults instead of failing.
    const Settings defaults = Settings::from_ini("");
    CHECK(defaults.enabled);
    CHECK(defaults.start_with_windows == false);
    CHECK(defaults.appearance.theme == ThemeId::AzyDarkGlass);
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
    CHECK_NEAR(junk.appearance.overlay_intensity, 1.0, 1e-6); // clamped
    CHECK(junk.appearance.overlay);                          // default kept (on)
    CHECK_INT(junk.appearance.corner_radius_dip, 0);          // clamped
    CHECK_NEAR(junk.appearance.darkness, 0.5, 1e-6);          // default kept
    CHECK(junk.appearance.theme == ThemeId::AzyDarkGlass);    // warning + default
    CHECK(!warnings.empty());
    CHECK(junk.enabled);                                     // "maybe" ignored
    CHECK(junk.extra.count("future_key") == 1);
    CHECK_STR(junk.extra.count("future_key") ? junk.extra.at("future_key") : std::string(), "kept");             // unknown keys survive
    CHECK(junk.to_ini().find("future_key=kept") != std::string::npos);
    CHECK(junk.to_ini().find("theme=azy_dark_glass") != std::string::npos);

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


// --- the four-strip ring -------------------------------------------------- //
void test_ring_layout() {
    std::printf("[ring layout]\n");

    // A window far larger than the treated band: the strips must tile the border
    // exactly, with no overlap (a doubled corner) and no gap (a visible notch).
    const Rect frame = Rect::from_size(1000, 700, 1920, 1080);  // 1000 wide? no: size form
    const Rect window = Rect::from_size(100, 200, 1200, 800);
    (void)frame;

    const int thickness = ring_strip_thickness(10, 8);  // band + 2 strokes, radius fits inside
    CHECK_INT(thickness, 12);

    const RingStrips strips = ring_strip_rects(window, thickness);
    CHECK(strips.valid);
    CHECK_INT(strips.top.left, window.left);
    CHECK_INT(strips.top.right, window.right);
    CHECK_INT(strips.top.top, window.top);
    CHECK_INT(strips.top.height(), thickness);
    CHECK_INT(strips.bottom.bottom, window.bottom);
    CHECK_INT(strips.bottom.top, window.bottom - thickness);
    CHECK_INT(strips.bottom.left, window.left);
    CHECK_INT(strips.bottom.right, window.right);

    // The vertical strips sit strictly between the horizontal ones - that is what
    // keeps the corner arcs owned by exactly one strip.
    CHECK_INT(strips.left.top, window.top + thickness);
    CHECK_INT(strips.left.bottom, window.bottom - thickness);
    CHECK_INT(strips.left.left, window.left);
    CHECK_INT(strips.left.width(), thickness);
    CHECK_INT(strips.right.right, window.right);
    CHECK_INT(strips.right.left, window.right - thickness);
    CHECK_INT(strips.right.top, strips.left.top);
    CHECK_INT(strips.right.bottom, strips.left.bottom);

    // No strip may overlap another.
    const Rect all[4] = {strips.top, strips.bottom, strips.left, strips.right};
    for (int i = 0; i < 4; ++i) {
        for (int j = i + 1; j < 4; ++j) {
            const bool disjoint = all[i].right <= all[j].left || all[j].right <= all[i].left ||
                                  all[i].bottom <= all[j].top || all[j].bottom <= all[i].top;
            CHECK(disjoint);
        }
    }

    // A large corner radius needs a thicker strip, otherwise the arc is clipped.
    CHECK_INT(ring_strip_thickness(10, 16), 17);
    CHECK_INT(ring_strip_thickness(3, 0), 5);

    // A big window keeps the requested band: this is the normal case, and the ring
    // must never grow just because the window did.
    const RingGeometry big = ring_geometry(Rect::from_size(0, 0, 3840, 2160), 20, 16);
    CHECK(big.valid);
    CHECK_INT(big.thickness_px, 22);  // band + the two 1px strokes
    CHECK_INT(big.radius_px, 16);

    // A small window gets a proportionally small ring instead of four strips that
    // meet in the middle (a twelfth of the shorter side, at least 3px).
    const RingGeometry small = ring_geometry(Rect::from_size(0, 0, 40, 30), 100, 0);
    CHECK(small.valid);
    CHECK_INT(small.thickness_px, 3);  // 30 / 12 -> below the 3px floor
    // The proportional cap only ever *reduces* the thickness: a 300px floating
    // panel keeps the requested band (13 + 2), which is well under 300 / 12.
    const RingGeometry floater = ring_geometry(Rect::from_size(0, 0, 300, 300), 13, 8);
    CHECK_INT(floater.thickness_px, 15);

    // The radius is capped so the arc always fits in the strip painting it.
    const RingGeometry clipped = ring_geometry(Rect::from_size(0, 0, 120, 120), 10, 16);
    CHECK(clipped.valid);
    CHECK_INT(clipped.thickness_px, 10);   // 120 / 12
    CHECK_INT(clipped.radius_px, 9);       // thickness - 1

    // Too small for a ring at all: the window would have no content left.
    CHECK(!ring_geometry(Rect::from_size(0, 0, 10, 10), 40, 0).valid);
    CHECK(!ring_geometry(Rect::from_size(0, 0, 0, 0), 10, 0).valid);

    // --- which rectangle the ring is drawn on -----------------------------
    // A maximized window reports bounds that hang over the monitor edges (the
    // invisible resize border). Drawing there would put the whole ring off
    // screen, so a maximized window is drawn on the work area instead.
    const Rect monitor = Rect::from_size(0, 0, 1920, 1080);
    const Rect work_area = Rect::from_size(0, 0, 1920, 1040);
    const Rect overhanging = Rect::from_size(-8, -8, 1936, 1048);
    CHECK(ring_frame(overhanging, monitor, work_area, true, false) == work_area);

    // A windowed Premiere inside the display keeps exactly the bounds Windows
    // reports for it.
    const Rect floating = Rect::from_size(300, 200, 900, 600);
    CHECK(ring_frame(floating, monitor, work_area, false, false) == floating);

    // A window whose frame reaches past the display - a WINDOWED window dragged
    // half off the edge, or a borderless "fullscreen" window Windows does not
    // report as maximized - is drawn on the part that is on screen.
    const Rect half_off = Rect::from_size(-400, 100, 900, 600);
    CHECK(ring_frame(half_off, monitor, work_area, false, false) == Rect::from_size(0, 100, 500, 600));
    const Rect borderless = Rect::from_size(-8, -8, 1936, 1096);  // == screen + borders, not IsZoomed
    CHECK(ring_frame(borderless, monitor, work_area, false, false) == monitor);

    // A fullscreen window covers the whole monitor, taskbar included.
    CHECK(ring_frame(monitor, monitor, work_area, false, true) == monitor);

    // Degenerate monitor data must never move the ring on its own.
    CHECK(ring_frame(floating, monitor, Rect{}, true, false) == floating);
    CHECK(ring_frame(floating, Rect{}, work_area, false, true) == floating);

    // ... and the geometry computed for a real maximized 1080p window is usable:
    // the band still fits, and the ring covers the visible edge.
    const Rect maximized_frame = ring_frame(overhanging, monitor, work_area, true, false);
    const RingGeometry maximized_ring = ring_geometry(maximized_frame, 10, 0);
    CHECK(maximized_ring.valid);
    CHECK_INT(maximized_ring.thickness_px, 12);
    const RingStrips maximized_strips = ring_strip_rects(maximized_frame, maximized_ring.thickness_px);
    CHECK(maximized_strips.valid);
    CHECK_INT(maximized_strips.top.top, 0);
    CHECK_INT(maximized_strips.top.height(), 12);
    CHECK_INT(maximized_strips.bottom.bottom, 1040);
}

}  // namespace

int main() {
    std::printf("Azy Skin core tests\n===================");
    test_version();
    test_build_key();
    test_product();
    test_compat();
    test_theme();
    test_dpi_geometry();
    test_settings();
    test_design_tokens();
    test_failure_tracker();
    test_strings();
    test_layout_safety();
    test_ring_layout();

    std::printf("\n===================\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
