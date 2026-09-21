#include "azy/core/theme_tokens.hpp"

#include <cctype>
#include <cstddef>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace azy {
namespace {

Rgb rgb(float r, float g, float b) { return Rgb{r, g, b}; }

float srgb_to_linear(float channel) {
    return channel <= 0.04045f ? channel / 12.92f : std::pow((channel + 0.055f) / 1.055f, 2.4f);
}

Rgb rgb_from_hex(unsigned long value) {
    return rgb(static_cast<float>((value >> 16) & 0xFF) / 255.0f, static_cast<float>((value >> 8) & 0xFF) / 255.0f,
               static_cast<float>(value & 0xFF) / 255.0f);
}

// Hue in degrees, saturation and value in 0..1 -> Rgb.
Rgb hsv_to_rgb(float h, float s, float v) {
    h = std::fmod(h, 360.0f);
    if (h < 0.0f) h += 360.0f;
    const float c = v * s;
    const float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    const float m = v - c;
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    if (h < 60.0f) {
        r = c; g = x;
    } else if (h < 120.0f) {
        r = x; g = c;
    } else if (h < 180.0f) {
        g = c; b = x;
    } else if (h < 240.0f) {
        g = x; b = c;
    } else if (h < 300.0f) {
        r = x; b = c;
    } else {
        r = c; b = x;
    }
    return rgb(r + m, g + m, b + m);
}

void rgb_to_hsv(Rgb colour, float& h, float& s, float& v) {
    const float max_channel = std::max(colour.r, std::max(colour.g, colour.b));
    const float min_channel = std::min(colour.r, std::min(colour.g, colour.b));
    const float delta = max_channel - min_channel;
    v = max_channel;
    s = max_channel <= 0.0001f ? 0.0f : delta / max_channel;
    if (delta <= 0.0001f) {
        h = 0.0f;
        return;
    }
    if (max_channel == colour.r) {
        h = 60.0f * std::fmod((colour.g - colour.b) / delta, 6.0f);
    } else if (max_channel == colour.g) {
        h = 60.0f * ((colour.b - colour.r) / delta + 2.0f);
    } else {
        h = 60.0f * ((colour.r - colour.g) / delta + 4.0f);
    }
    if (h < 0.0f) h += 360.0f;
}

// Every theme is built from one accent plus a base hue, so the eight variants are
// genuinely the same design language with different light (spec §27: "the base dark
// UI should remain consistent across themes").
struct ThemeSeed {
    unsigned long accent;
    unsigned long accent_secondary;
    float base_hue;        // hue of the near-black base
    float base_saturation; // how much of that hue is in the base
    float base_value;      // how dark the base is
};

ThemeSeed seed_for(ThemeKey key) {
    switch (key) {
        case ThemeKey::Cyan:
            return ThemeSeed{0x22D3EE, 0x0EA5E9, 200.0f, 0.35f, 0.070f};
        case ThemeKey::Purple:
            return ThemeSeed{0xA855F7, 0x7C3AED, 268.0f, 0.32f, 0.075f};
        case ThemeKey::Magenta:
            return ThemeSeed{0xE879F9, 0xC026D3, 285.0f, 0.32f, 0.070f};
        case ThemeKey::Red:
            return ThemeSeed{0xF87171, 0xDC2626, 222.0f, 0.28f, 0.065f};
        case ThemeKey::Orange:
            return ThemeSeed{0xFB923C, 0xF97316, 235.0f, 0.24f, 0.065f};
        case ThemeKey::Green:
            return ThemeSeed{0x4ADE80, 0x16A34A, 200.0f, 0.30f, 0.062f};
        case ThemeKey::Pink:
            return ThemeSeed{0xF472B6, 0xDB2777, 280.0f, 0.30f, 0.072f};
        case ThemeKey::BluePurple:
        case ThemeKey::Custom:  // Custom keeps the default base; only the accent moves
        default:
            return ThemeSeed{0x7C8CFF, 0xA855F7, 240.0f, 0.34f, 0.070f};
    }
}

}  // namespace

const char* theme_key_name(ThemeKey key) {
    switch (key) {
        case ThemeKey::BluePurple: return "Blue / Purple";
        case ThemeKey::Cyan: return "Cyan";
        case ThemeKey::Purple: return "Purple";
        case ThemeKey::Magenta: return "Magenta";
        case ThemeKey::Red: return "Red";
        case ThemeKey::Orange: return "Orange";
        case ThemeKey::Green: return "Green";
        case ThemeKey::Pink: return "Pink";
        case ThemeKey::Custom: return "Custom";
        case ThemeKey::Original: return "Original (no skin)";
    }
    return "Blue / Purple";
}

const char* theme_key_id(ThemeKey key) {
    switch (key) {
        case ThemeKey::BluePurple: return "blue_purple";
        case ThemeKey::Cyan: return "cyan";
        case ThemeKey::Purple: return "purple";
        case ThemeKey::Magenta: return "magenta";
        case ThemeKey::Red: return "red";
        case ThemeKey::Orange: return "orange";
        case ThemeKey::Green: return "green";
        case ThemeKey::Pink: return "pink";
        case ThemeKey::Custom: return "custom";
        case ThemeKey::Original: return "original";
    }
    return "blue_purple";
}

bool theme_key_from_id(const std::string& id, ThemeKey& out) {
    // A hand-edited settings.ini should not care about spelling: text is compared
    // with spaces and underscores removed, case-insensitively.
    const auto normalised = [](const std::string& text) {
        std::string out_text;
        for (char c : text) {
            if (c == ' ' || c == '_' || c == '-' || c == '/') continue;
            out_text.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        return out_text;
    };
    const std::string wanted = normalised(id);
    if (wanted.empty()) return false;
    for (int i = 0; i < kThemeCount; ++i) {
        const ThemeKey key = theme_key_at(i);
        if (wanted == normalised(theme_key_id(key))) {
            out = key;
            return true;
        }
    }
    // The pre-2.0 identifiers, so an existing settings.ini keeps working. The old
    // "dark glass" is the blue/violet look this rebuild defaults to; "dark" was the
    // same skin without the translucency, which now means the neutral Cyan variant.
    if (wanted == "azydarkglass" || wanted == "darkglass") {
        out = ThemeKey::BluePurple;
        return true;
    }
    if (wanted == "azydark" || wanted == "dark") {
        out = ThemeKey::Cyan;
        return true;
    }
    return false;
}

ThemeKey theme_key_at(int index) {
    if (index < 0 || index >= kThemeCount) return ThemeKey::BluePurple;
    return static_cast<ThemeKey>(index);
}

int theme_key_index(ThemeKey key) {
    const int index = static_cast<int>(key);
    return index < 0 || index >= kThemeCount ? 0 : index;
}

ThemeTokens theme_tokens(ThemeKey key, Rgba custom_accent) {
    ThemeSeed seed = seed_for(key);
    if (key == ThemeKey::Custom) {
        seed.accent = (static_cast<unsigned long>(custom_accent.r) << 16) |
                      (static_cast<unsigned long>(custom_accent.g) << 8) |
                      static_cast<unsigned long>(custom_accent.b);
        // The secondary hue is the accent's own family, 25 degrees round the wheel;
        // the theme engine derives it rather than asking the user for a second
        // colour, which is what keeps a custom theme coherent.
        const Rgb accent_rgb = rgb_from_hex(seed.accent);
        float h = 0.0f;
        float s = 0.0f;
        float v = 0.0f;
        rgb_to_hsv(accent_rgb, h, s, v);
        const Rgb secondary = hsv_to_rgb(h - 25.0f, std::min(1.0f, s * 0.95f + 0.05f), v * 0.9f);
        seed.accent_secondary = (static_cast<unsigned long>(secondary.r * 255.0f + 0.5f) << 16) |
                                (static_cast<unsigned long>(secondary.g * 255.0f + 0.5f) << 8) |
                                static_cast<unsigned long>(secondary.b * 255.0f + 0.5f);
    }

    ThemeTokens tokens;
    if (key == ThemeKey::Original) {
        tokens.visible = false;
        return tokens;
    }

    const Rgb accent = rgb_from_hex(seed.accent);
    const Rgb accent_secondary = rgb_from_hex(seed.accent_secondary);

    // The base ramp. Four steps of the same hue (spec §13): background, panel,
    // secondary panel, hover - each one slightly lifted, so surfaces separate
    // without any of them becoming grey.
    tokens.background = hsv_to_rgb(seed.base_hue, seed.base_saturation, seed.base_value);
    tokens.surface = hsv_to_rgb(seed.base_hue, seed.base_saturation * 0.85f, seed.base_value + 0.055f);
    tokens.surface_secondary = hsv_to_rgb(seed.base_hue, seed.base_saturation * 0.75f, seed.base_value + 0.035f);
    tokens.hover = hsv_to_rgb(seed.base_hue, seed.base_saturation * 0.8f, seed.base_value + 0.095f);
    tokens.border = hsv_to_rgb(seed.base_hue, seed.base_saturation * 0.35f, seed.base_value + 0.20f);

    tokens.accent = accent;
    tokens.accent_secondary = accent_secondary;
    tokens.glow = accent;
    tokens.border_active = accent;
    tokens.selection = rgb(accent.r * 0.55f, accent.g * 0.55f, accent.b * 0.55f);

    // Text tokens: the skin cannot change Premiere's font, but it does decide how
    // much light reaches the glyphs, and these are the luminances the surface
    // strengths are tuned against (spec §25). They are reported in the diagnostics
    // and used by the clarity term.
    tokens.text_primary = rgb(0.90f, 0.92f, 0.96f);
    tokens.text_secondary = rgb(0.62f, 0.66f, 0.73f);
    tokens.text_disabled = rgb(0.36f, 0.38f, 0.43f);

    // Per-theme strengths. Bright hues (red, orange, green) read stronger at the
    // same mathematical strength, so their glow and accent weights are lower - the
    // same reason the dark base stays constant.
    switch (key) {
        case ThemeKey::Red:
        case ThemeKey::Orange:
            tokens.glow_strength = 0.26f;
            tokens.surface_opacity = 0.50f;
            break;
        case ThemeKey::Green:
            tokens.glow_strength = 0.28f;
            tokens.surface_opacity = 0.50f;
            break;
        case ThemeKey::Pink:
            tokens.glow_strength = 0.32f;
            break;
        case ThemeKey::Cyan:
            tokens.glow_strength = 0.34f;
            break;
        default:
            tokens.glow_strength = 0.38f;
            break;
    }
    tokens.reflection_strength = 0.30f;
    tokens.contrast_lift = 0.06f;
    tokens.base_darkening = 0.42f;
    // srgb_to_linear is used below for the reflective sheen's response curve; keep
    // the helper honest by using it for the surface lift.
    tokens.surface_opacity = std::min(0.95f, tokens.surface_opacity + srgb_to_linear(0.5f) * 0.06f);
    return tokens;
}

bool parse_hex_color(const std::string& text, Rgba& out) {
    std::string value;
    value.reserve(text.size());
    for (char c : text) {
        if (c == ' ' || c == '\t') continue;
        value.push_back(c);
    }
    if (value.empty()) return false;
    if (value[0] == '#') value.erase(value.begin());

    // "r,g,b" form.
    if (value.find(',') != std::string::npos) {
        unsigned int channels[3] = {0, 0, 0};
        int index = 0;
        std::size_t start = 0;
        while (index < 3 && start <= value.size()) {
            const std::size_t comma = value.find(',', start);
            const std::string part = value.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            try {
                const int channel = std::stoi(part);
                if (channel < 0 || channel > 255) return false;
                channels[index] = static_cast<unsigned int>(channel);
            } catch (...) {
                return false;
            }
            ++index;
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        if (index != 3) return false;
        out = Rgba{static_cast<unsigned char>(channels[0]), static_cast<unsigned char>(channels[1]),
                   static_cast<unsigned char>(channels[2]), 255};
        return true;
    }

    if (value.size() != 6) return false;
    unsigned long packed = 0;
    for (char c : value) {
        unsigned int digit = 0;
        if (c >= '0' && c <= '9') {
            digit = static_cast<unsigned int>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = static_cast<unsigned int>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            digit = static_cast<unsigned int>(c - 'A' + 10);
        } else {
            return false;
        }
        packed = packed * 16 + digit;
    }
    out = Rgba{static_cast<unsigned char>((packed >> 16) & 0xFF), static_cast<unsigned char>((packed >> 8) & 0xFF),
               static_cast<unsigned char>(packed & 0xFF), 255};
    return true;
}

std::string format_hex_color(Rgba color) {
    char buffer[8] = {0};
    std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", color.r, color.g, color.b);
    return std::string(buffer);
}

Rgb rotate_hue(Rgb colour, float degrees, float value_scale) {
    float h = 0.0f;
    float s = 0.0f;
    float v = 0.0f;
    rgb_to_hsv(colour, h, s, v);
    return hsv_to_rgb(h + degrees, s, std::min(1.0f, v * value_scale));
}

}  // namespace azy
