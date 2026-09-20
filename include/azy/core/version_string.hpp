// Azy Skin — portable core: the version string of this build.
//
// CMakeLists.txt passes the project version in (AZY_VERSION_STRING), so the banner,
// the settings window and the installer can never drift apart. The fallback matters
// only when the sources are compiled by hand; tools/check-version.py keeps every
// copy of the number in step either way.
#pragma once

#ifndef AZY_VERSION_STRING
#define AZY_VERSION_STRING "1.2.0"
#endif

namespace azy {

constexpr const char* kAppVersion = AZY_VERSION_STRING;

// Packs "1.2.3" into 0x010203, so two builds of Azy can be told apart across a
// process boundary, where only a message result can travel: a build that has just
// been installed uses it to recognise that a *different* build is already running
// and owns the skin (see the take-over in main.cpp). Parsing stops at the first
// character that is neither a digit nor a dot, so "1.2.0-beta" compares as 1.2.0;
// missing components are zero.
constexpr unsigned pack_version(const char* text) {
    unsigned parts[3] = {0, 0, 0};
    int index = 0;
    for (const char* p = text; p != nullptr && *p != '\0' && index < 3; ++p) {
        if (*p >= '0' && *p <= '9') {
            parts[index] = parts[index] * 10u + static_cast<unsigned>(*p - '0');
        } else if (*p == '.') {
            ++index;
        } else {
            break;
        }
    }
    return (parts[0] << 16) | (parts[1] << 8) | parts[2];
}

}  // namespace azy
