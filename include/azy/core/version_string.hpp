// Azy Skin — portable core: the version string of this build.
//
// CMakeLists.txt passes the project version in (AZY_VERSION_STRING), so the
// runtime banner, the settings window and the installer cannot drift apart. The
// fallback only matters when the sources are compiled by hand, and
// tools/check-version.py keeps every copy of the number in step.
#pragma once

#ifndef AZY_VERSION_STRING
#define AZY_VERSION_STRING "1.0.2"
#endif

namespace azy {

constexpr const char* kAppVersion = AZY_VERSION_STRING;

}  // namespace azy
