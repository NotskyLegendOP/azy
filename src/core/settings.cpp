#include "azy/core/settings.hpp"

#include <algorithm>
#include <cstdio>
#include <set>

#include "azy/core/strings.hpp"

namespace azy {
namespace {

std::string bool_str(bool v) { return v ? "1" : "0"; }

std::string fmt_double(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", v);
    // Trim trailing zeros: "0.550" -> "0.55", "1.000" -> "1".
    std::string s(buf);
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s;
}

double read_double(const std::string& raw, double fallback) {
    try {
        const std::string t = trim(raw);
        if (t.empty()) return fallback;
        return std::stod(t);
    } catch (...) {
        return fallback;
    }
}

struct KeyValue {
    std::string section;
    std::string key;
    std::string value;
};

std::vector<KeyValue> parse_ini(const std::string& text) {
    std::vector<KeyValue> out;
    std::string section;
    for (const std::string& raw_line : split(text, '\n')) {
        std::string line = trim(raw_line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = to_lower(trim(line.substr(1, line.size() - 2)));
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        KeyValue kv;
        kv.section = section;
        kv.key = to_lower(trim(line.substr(0, eq)));
        kv.value = trim(line.substr(eq + 1));
        out.push_back(kv);
    }
    return out;
}

std::vector<std::string> split_feature_list(const std::string& value) {
    std::vector<std::string> out;
    for (const std::string& part : split(value, ',')) {
        const std::string t = to_lower(trim(part));
        if (!t.empty()) out.push_back(t);
    }
    return out;
}

}  // namespace

void Settings::clamp() {
    appearance.glass_intensity = std::max(0.0, std::min(1.0, appearance.glass_intensity));
    appearance.border_intensity = std::max(0.0, std::min(1.0, appearance.border_intensity));
    appearance.shadow_intensity = std::max(0.0, std::min(1.0, appearance.shadow_intensity));
    appearance.darkness = std::max(0.0, std::min(1.0, appearance.darkness));
    appearance.corner_radius_dip = std::max(0, std::min(16, appearance.corner_radius_dip));
    if (schema < 1) schema = 1;
    if (failure_count < 0) failure_count = 0;
}

bool Settings::feature_disabled(const std::string& key) const {
    auto it = extra.find("disabled_features");
    if (it == extra.end()) return false;
    const auto list = split_feature_list(it->second);
    return std::find(list.begin(), list.end(), to_lower(trim(key))) != list.end();
}

std::string Settings::disabled_feature_list() const {
    auto it = extra.find("disabled_features");
    if (it == extra.end()) return std::string();
    const auto list = split_feature_list(it->second);
    return join(list, ",");
}

void Settings::set_feature_disabled(const std::string& key, bool disabled) {
    auto list = split_feature_list(disabled_feature_list());
    const std::string k = to_lower(trim(key));
    auto it = std::find(list.begin(), list.end(), k);
    if (disabled && it == list.end()) {
        list.push_back(k);
    } else if (!disabled && it != list.end()) {
        list.erase(it);
    }
    extra["disabled_features"] = join(list, ",");
}

std::string Settings::to_ini() const {
    std::string out;
    out += "; Azy Skin configuration\n";
    out += "; This file is safe to edit by hand; Azy rewrites it on exit.\n\n";

    out += "[skin]\n";
    out += "enabled=" + bool_str(enabled) + "\n";
    out += "start_with_windows=" + bool_str(start_with_windows) + "\n";
    out += "apply_automatically=" + bool_str(apply_automatically) + "\n\n";

    out += "[appearance]\n";
    out += std::string("theme=") + theme_key(appearance.theme) + "\n";
    out += "glass_intensity=" + fmt_double(appearance.glass_intensity) + "\n";
    out += "border_intensity=" + fmt_double(appearance.border_intensity) + "\n";
    out += "corner_radius=" + std::to_string(appearance.corner_radius_dip) + "\n";
    out += "shadow_intensity=" + fmt_double(appearance.shadow_intensity) + "\n";
    out += "darkness=" + fmt_double(appearance.darkness) + "\n\n";

    out += "[performance]\n";
    out += "performance_mode=" + bool_str(performance_mode) + "\n";
    out += "suspend_while_minimized=" + bool_str(suspend_when_minimized) + "\n";
    out += "suspend_while_inactive=" + bool_str(suspend_when_inactive) + "\n\n";

    out += "[advanced]\n";
    out += "experimental=" + bool_str(experimental) + "\n";
    out += "safe_mode=" + bool_str(safe_mode) + "\n";
    out += "safe_mode_reason=" + safe_mode_reason + "\n";
    out += "failures=" + std::to_string(failure_count) + "\n";
    out += "failure_window_start=" + fmt_double(failure_window_start) + "\n";
    out += "failure_last=" + fmt_double(failure_last) + "\n";
    out += "failure_tripped=" + bool_str(failure_tripped) + "\n";
    out += "last_premiere_version=" + last_premiere_version + "\n\n";

    out += "[meta]\n";
    out += "schema=" + std::to_string(schema) + "\n";

    // Round-trip anything we did not recognise (includes disabled_features).
    std::set<std::string> known;
    static const char* kKnown[] = {
        "enabled", "start_with_windows", "apply_automatically", "theme", "glass_intensity",
        "border_intensity", "corner_radius", "shadow_intensity", "darkness", "performance_mode",
        "suspend_while_minimized", "suspend_while_inactive", "experimental", "safe_mode",
        "safe_mode_reason", "failures", "failure_window_start", "failure_last", "failure_tripped",
        "last_premiere_version", "schema"};
    for (const char* k : kKnown) known.insert(k);

    std::vector<std::string> extras;
    for (const auto& kv : extra) {
        if (known.count(kv.first)) continue;
        extras.push_back(kv.first + "=" + kv.second);
    }
    if (!extras.empty()) {
        out += "\n[extra]\n";
        for (const auto& line : extras) out += line + "\n";
    }
    return out;
}

Settings Settings::from_ini(const std::string& text, std::vector<std::string>* warnings) {
    Settings s;
    for (const KeyValue& kv : parse_ini(text)) {
        const std::string& key = kv.key;
        const std::string& value = kv.value;

        if (key == "enabled") {
            bool v = false;
            if (parse_bool(value, v)) s.enabled = v;
        } else if (key == "start_with_windows") {
            bool v = false;
            if (parse_bool(value, v)) s.start_with_windows = v;
        } else if (key == "apply_automatically") {
            bool v = false;
            if (parse_bool(value, v)) s.apply_automatically = v;
        } else if (key == "theme") {
            ThemeId theme = ThemeId::AzyDarkGlass;
            if (theme_from_key(value, theme)) {
                s.appearance.theme = theme;
            } else if (warnings) {
                warnings->push_back("unknown theme '" + value + "'; using Azy Dark Glass");
            }
        } else if (key == "glass_intensity") {
            s.appearance.glass_intensity = read_double(value, s.appearance.glass_intensity);
        } else if (key == "border_intensity") {
            s.appearance.border_intensity = read_double(value, s.appearance.border_intensity);
        } else if (key == "corner_radius") {
            int v = 0;
            if (parse_int(value, v)) s.appearance.corner_radius_dip = v;
        } else if (key == "shadow_intensity") {
            s.appearance.shadow_intensity = read_double(value, s.appearance.shadow_intensity);
        } else if (key == "darkness") {
            s.appearance.darkness = read_double(value, s.appearance.darkness);
        } else if (key == "performance_mode") {
            bool v = false;
            if (parse_bool(value, v)) s.performance_mode = v;
        } else if (key == "suspend_while_minimized") {
            bool v = false;
            if (parse_bool(value, v)) s.suspend_when_minimized = v;
        } else if (key == "suspend_while_inactive") {
            bool v = false;
            if (parse_bool(value, v)) s.suspend_when_inactive = v;
        } else if (key == "experimental") {
            bool v = false;
            if (parse_bool(value, v)) s.experimental = v;
        } else if (key == "safe_mode") {
            bool v = false;
            if (parse_bool(value, v)) s.safe_mode = v;
        } else if (key == "safe_mode_reason") {
            s.safe_mode_reason = value;
        } else if (key == "failures") {
            int v = 0;
            if (parse_int(value, v)) s.failure_count = v;
        } else if (key == "failure_window_start") {
            s.failure_window_start = read_double(value, 0.0);
        } else if (key == "failure_last") {
            s.failure_last = read_double(value, 0.0);
        } else if (key == "failure_tripped") {
            bool v = false;
            if (parse_bool(value, v)) s.failure_tripped = v;
        } else if (key == "last_premiere_version") {
            s.last_premiere_version = value;
        } else if (key == "schema") {
            int v = 0;
            if (parse_int(value, v)) s.schema = v;
        } else {
            s.extra[key] = value;  // preserved verbatim
        }
    }
    s.clamp();
    return s;
}

}  // namespace azy
