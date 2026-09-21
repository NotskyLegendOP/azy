#include "azy/win32/os/win_version.hpp"
#include <string>

#include <vector>

#include "azy/core/strings.hpp"
#include "azy/win32/os/win_api.hpp"
#include "azy/win32/os/win_util.hpp"

namespace azy {
namespace win {
namespace {

// Declared locally instead of pulling in <winternl.h>, which drags in a lot of
// kernel-only declarations we have no use for.
struct AzyOsVersionInfoW {
    ULONG dwOSVersionInfoSize;
    ULONG dwMajorVersion;
    ULONG dwMinorVersion;
    ULONG dwBuildNumber;
    ULONG dwPlatformId;
    WCHAR szCSDVersion[128];
};

int read_build_number() {
    // RtlGetVersion is the only version query that is not shimmed by the
    // application compatibility layer, so it reports the true build number.
    typedef LONG(WINAPI * RtlGetVersionFn)(AzyOsVersionInfoW*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        auto rtl_get_version = reinterpret_cast<RtlGetVersionFn>(
            reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
        if (rtl_get_version) {
            AzyOsVersionInfoW info{};
            info.dwOSVersionInfoSize = sizeof(info);
            if (rtl_get_version(&info) == 0 /*STATUS_SUCCESS*/) {
                return static_cast<int>(info.dwBuildNumber);
            }
        }
    }
    OSVERSIONINFOW fallback{};
    fallback.dwOSVersionInfoSize = sizeof(fallback);
#ifdef _MSC_VER
#pragma warning(suppress : 4996)  // GetVersionEx is deprecated but a valid fallback
#endif
    if (GetVersionExW(&fallback)) return static_cast<int>(fallback.dwBuildNumber);
    return 0;
}

std::string build_to_name(int build) {
    if (build >= 22000) return "Windows 11";
    if (build >= 10240) return "Windows 10";
    return "Windows";
}

std::string build_to_release(int build) {
    if (build >= 26100) return "24H2";
    if (build >= 22631) return "23H2";
    if (build >= 22621) return "22H2";
    if (build >= 22000) return "21H2 (11)";
    if (build >= 19045) return "22H2";
    if (build >= 19044) return "21H2";
    if (build >= 19043) return "21H1";
    if (build >= 19042) return "20H2";
    if (build >= 19041) return "2004";
    if (build >= 18363) return "1909";
    if (build >= 18362) return "1903";
    if (build >= 17763) return "1809";
    return std::string();
}

HostInfo build_host_info() {
    HostInfo info;
    const int build = read_build_number();
    info.windows_build = build;

    const std::string release = build_to_release(build);
    info.os_name = build_to_name(build);
    if (!release.empty()) info.os_name += " " + release;
    if (build > 0) info.os_name += str_format(" (build %d)", build);

    // The app-theme flag is reported, not obeyed: the reference design is a dark
    // skin on purpose, whatever the host's light/dark preference is.
    info.host_summary = str_format("%s; light_apps=%d", info.os_name.c_str(), win::system_uses_dark_apps() ? 0 : 1);
    return info;
}

}  // namespace

int windows_build_number() { return host_info().windows_build; }
std::string windows_version_string() { return host_info().os_name; }

const HostInfo& host_info() {
    static const HostInfo info = build_host_info();
    return info;
}

}  // namespace win
}  // namespace azy
