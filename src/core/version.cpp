#include "azy/core/version.hpp"

#include <cctype>
#include <cstdio>

namespace azy {
namespace {

bool is_digit(char c) { return c >= '0' && c <= '9'; }

// Reads a run of digits, saturating rather than overflowing on absurd input.
bool read_number(const std::string& s, size_t& i, int& out) {
    if (i >= s.size() || !is_digit(s[i])) return false;
    long long value = 0;
    while (i < s.size() && is_digit(s[i])) {
        value = value * 10 + (s[i] - '0');
        if (value > 1000000000LL) value = 1000000000LL;  // saturate
        ++i;
    }
    out = static_cast<int>(value);
    return true;
}

}  // namespace

bool parse_version(const std::string& text, Version& out) {
    Version parsed;
    size_t i = 0;
    const size_t n = text.size();

    // Skip any leading non-digit noise ("v", spaces, "Adobe ... ").
    while (i < n && !is_digit(text[i])) ++i;

    int* slots[4] = {&parsed.major, &parsed.minor, &parsed.patch, &parsed.build};
    int filled = 0;
    bool any = false;
    for (int slot = 0; slot < 4; ++slot) {
        if (!read_number(text, i, *slots[slot])) break;
        any = true;
        filled = slot + 1;
        // Separator: a single '.' or ',' (Adobe has used both in the wild).
        if (i < n && (text[i] == '.' || text[i] == ',')) {
            ++i;
            continue;
        }
        break;
    }
    (void)filled;
    if (!any) return false;
    out = parsed;
    return true;
}

std::string format_version(const Version& v) {
    char buf[64];
    if (v.build != 0) {
        std::snprintf(buf, sizeof(buf), "%d.%d.%d.%d", v.major, v.minor, v.patch, v.build);
    } else if (v.patch != 0) {
        std::snprintf(buf, sizeof(buf), "%d.%d.%d", v.major, v.minor, v.patch);
    } else {
        std::snprintf(buf, sizeof(buf), "%d.%d", v.major, v.minor);
    }
    return std::string(buf);
}

std::string format_version_short(const Version& v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d.%d", v.major, v.minor);
    return std::string(buf);
}

}  // namespace azy
