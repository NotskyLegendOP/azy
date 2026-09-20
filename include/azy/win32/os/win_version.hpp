// Azy Skin — Win32 layer: host capability probe.
//
// Resolves what THIS Windows build can actually do (DWM composition, dark
// title bars, rounded frames, system backdrop) so the compatibility policy in
// azy/core/compat.hpp can decide what is safe to apply to Premiere.
#pragma once

#include <string>

#include "azy/core/compat.hpp"
#include "azy/win32/os/win_compat.hpp"

namespace azy {
namespace win {

struct HostInfo {
    HostCapabilities capabilities;
    std::string os_name;       // "Windows 11 23H2", "Windows 10 22H2 (build 19045)"
    std::string host_summary;  // one-line description for the log
};

// Probes the host once and caches the result.
const HostInfo& host_info();

// Raw Windows build number (e.g. 22631).
int windows_build_number();
std::string windows_version_string();

}  // namespace win
}  // namespace azy
