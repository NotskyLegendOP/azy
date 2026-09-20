// Azy Skin — Win32 layer: everything Azy learns about a running Premiere build.
//
// All of it comes from public, read-only Windows facilities:
//   * the process list (basename match, never a hardcoded install path)
//   * QueryFullProcessImageName (exact executable location, read-only)
//   * the executable's VERSIONINFO resource (ProductVersion/FileVersion)
//   * EnumWindows + DWM cloak state (which top-level window is "the" window)
#pragma once

#include <string>
#include <vector>

#include "azy/core/product.hpp"
#include "azy/win32/os/win_compat.hpp"

namespace azy {
namespace win {

// Executable basenames Adobe has shipped for Premiere Pro. Matching is on the
// basename only, and the version resource decides the rest, so unknown future
// names fall back to conservative behaviour instead of failing.
const std::vector<std::wstring>& premiere_executable_names_lower();
const std::vector<std::string>& premiere_executable_names_utf8();

struct ProcessRecord {
    unsigned long pid = 0;
    std::wstring exe_name;
    std::wstring exe_path;
    bool version_known = false;
    Version file_version;
    ProductInfo product;
};

class PremiereProbe {
public:
    // Full identity for one process. Fails when the process is gone or is not
    // Premiere, never by throwing.
    static bool read_identity(unsigned long pid, ProcessRecord& out);

    // Version resource of an arbitrary executable (used for `read_identity` and
    // for the diagnostics panel).
    static bool read_file_version(const std::wstring& path, Version& out, std::string& raw_text);

    // The main Premiere window for a process: the largest visible, non-cloaked,
    // captioned top-level window. Returns nullptr until Premiere has one (i.e.
    // while it is still starting up).
    static HWND find_main_window(unsigned long pid);

    // All visible top-level windows of the process (main window + any floating
    // panels). Used for diagnostics and for the window count shown in Settings.
    static std::vector<HWND> find_top_level_windows(unsigned long pid);
};

}  // namespace win
}  // namespace azy
