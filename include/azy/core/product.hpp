// Azy Skin — portable core: how a running Adobe Premiere Pro build is
// described. Everything here is derived from data Windows exposes publicly
// (process image name/path and the executable's VERSIONINFO resource), so no
// code in this file knows or cares where Premiere is installed.
#pragma once

#include <string>

#include "azy/core/version.hpp"

namespace azy {

// Adobe's marketing release, derived from the file-version major number.
// Premiere Pro uses a year-aligned major version since CC 2019
// (13 -> 2019, 22 -> 2022, 24 -> 2024, 25 -> 2025, 26 -> 2026, ...).
enum class ProductFamily {
    Unknown = 0,  // unrecognised / future major version -> conservative skin
    Legacy,       // 13.x .. 21.x (CC 2019 .. 2021) — best effort only
    V2022,
    V2023,
    V2024,
    V2025,
    V2026,
    VNext,  // major > 26: assumed structurally similar to the newest known
};

// Channel: release or Beta (Beta builds live in a separate executable).
enum class ProductChannel { Release = 0, Beta = 1 };

struct ProductInfo {
    std::string executable_name;  // e.g. "Adobe Premiere Pro.exe" (UTF-8)
    std::string executable_path;  // full path as reported by Windows
    Version file_version;         // from VERSIONINFO, e.g. 25.6.0.58
    ProductFamily family = ProductFamily::Unknown;
    ProductChannel channel = ProductChannel::Release;
    bool version_known = false;  // false => VERSIONINFO unavailable

    // "Premiere Pro 2025", "Premiere Pro 2026 Beta", "Premiere Pro (unknown)".
    std::string display_name() const;
    // "25.6.0.58" / "unknown".
    std::string version_string() const;
    // Stable identifier used to key per-version compatibility data and the
    // failure tracker: "2025", "2026-beta", "legacy", "unknown".
    std::string family_key() const;
    int year() const;  // 2026, 2025, ... or 0 when unknown
};

// Classifies an executable *name* (basename only, case-insensitive) plus an
// optional file version into a ProductInfo. Never touches the filesystem.
bool is_premiere_executable(const std::string& executable_name);
ProductInfo make_product_info(const std::string& executable_name,
                              const std::string& executable_path,
                              bool version_known,
                              const Version& file_version);

const char* product_family_name(ProductFamily family);

}  // namespace azy
