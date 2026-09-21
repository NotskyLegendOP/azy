// Azy Skin — Win32 layer: the host probe.
//
// What is left of the old capability probing: the OS name and build number, used
// for the log and the diagnostics line. The mirror does not need to be *told* what
// the host can do - it asks Windows by trying: the capture session either starts or
// returns a reason, and the window is either composited or it is not. Probing DWM
// attributes would be measuring a path Azy no longer takes.
#pragma once

#include <string>

#include "azy/win32/os/win_compat.hpp"

namespace azy {
namespace win {

struct HostInfo {
    int windows_build = 0;     // e.g. 19045 (Win10 22H2), 22631 (Win11 23H2)
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
