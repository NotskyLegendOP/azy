// Azy Skin — portable core: dotted version parsing and comparison.
// No Windows dependencies. Unit-tested by tests/core_tests.cpp.
#pragma once

#include <cstdint>
#include <string>

namespace azy {

// A 4-component dotted version, e.g. 25.6.0.58 as reported by Adobe's
// VERSIONINFO resource ("FileVersion" in the executable's properties).
struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
    int build = 0;

    constexpr bool operator==(const Version& o) const {
        return major == o.major && minor == o.minor && patch == o.patch && build == o.build;
    }
    constexpr bool operator!=(const Version& o) const { return !(*this == o); }

    // Ordered comparison: major first, then minor, patch, build.
    friend bool operator<(const Version& a, const Version& b) {
        if (a.major != b.major) return a.major < b.major;
        if (a.minor != b.minor) return a.minor < b.minor;
        if (a.patch != b.patch) return a.patch < b.patch;
        return a.build < b.build;
    }
    friend bool operator>(const Version& a, const Version& b) { return b < a; }
    friend bool operator<=(const Version& a, const Version& b) { return !(b < a); }
    friend bool operator>=(const Version& a, const Version& b) { return !(a < b); }
};

// Parses "25.6.0.58", "25.6", " 25.6.0.58 (Beta) " etc.
// Missing components default to 0. Returns false when no numeric component
// could be read at all (callers then treat the build as unknown).
bool parse_version(const std::string& text, Version& out);

// "25.6.0.58" with trailing components trimmed when they are zero,
// shortest form that still round-trips meaningful information: "25.6".
std::string format_version(const Version& v);

// Two-component short form ("25.6"), used by log lines and the settings UI.
std::string format_version_short(const Version& v);

}  // namespace azy
