// Azy Skin — portable core: the version string of this build.
//
// CMakeLists.txt passes the project version in (AZY_VERSION_STRING), so the banner,
// the settings window and the installer can never drift apart. The fallback matters
// only when the sources are compiled by hand; tools/check-version.py keeps every
// copy of the number in step either way.
#pragma once

#ifndef AZY_VERSION_STRING
#define AZY_VERSION_STRING "1.1.0"
#endif

namespace azy {

constexpr const char* kAppVersion = AZY_VERSION_STRING;

}  // namespace azy
