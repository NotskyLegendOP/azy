#include "azy/core/product.hpp"

#include "azy/core/strings.hpp"

namespace azy {
namespace {

// Executable names Adobe has shipped for the Premiere Pro desktop app.
// Matching is on the basename only and is case-insensitive; the full install
// path is never assumed, so custom install locations keep working.
bool name_is_premiere(const std::string& lower_name) {
    return lower_name == "adobe premiere pro.exe" ||
           lower_name == "adobe premiere pro beta.exe" ||
           lower_name == "adobe premiere pro headless.exe";  // watchdog/helper host
}

std::string basename_of(const std::string& path) {
    size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

ProductFamily family_from_major(int major) {
    switch (major) {
        case 13:
        case 14:
        case 15:
        case 16:
        case 17:
        case 18:
        case 19:
        case 20:
        case 21:
            return ProductFamily::Legacy;
        case 22:
            return ProductFamily::V2022;
        case 23:
            return ProductFamily::V2023;
        case 24:
            return ProductFamily::V2024;
        case 25:
            return ProductFamily::V2025;
        case 26:
            return ProductFamily::V2026;
        default:
            // Major 27+ is newer than this build of Azy Skin knows about:
            // treat it as "next", which uses the newest safe configuration
            // rather than the fully conservative one.
            return major > 26 ? ProductFamily::VNext : ProductFamily::Unknown;
    }
}

}  // namespace

const char* product_family_name(ProductFamily family) {
    switch (family) {
        case ProductFamily::Legacy: return "legacy";
        case ProductFamily::V2022: return "2022";
        case ProductFamily::V2023: return "2023";
        case ProductFamily::V2024: return "2024";
        case ProductFamily::V2025: return "2025";
        case ProductFamily::V2026: return "2026";
        case ProductFamily::VNext: return "next";
        case ProductFamily::Unknown: break;
    }
    return "unknown";
}

bool is_premiere_executable(const std::string& executable_name) {
    return name_is_premiere(to_lower(trim(executable_name)));
}

ProductInfo make_product_info(const std::string& executable_name,
                              const std::string& executable_path,
                              bool version_known,
                              const Version& file_version) {
    ProductInfo info;
    info.executable_name = basename_of(executable_name.empty() ? executable_path : executable_name);
    info.executable_path = executable_path;
    info.version_known = version_known;
    info.file_version = file_version;

    const std::string lower = to_lower(info.executable_name);
    info.channel = lower.find("beta") != std::string::npos ? ProductChannel::Beta : ProductChannel::Release;

    if (!version_known) {
        info.family = ProductFamily::Unknown;
        return info;
    }
    info.family = family_from_major(file_version.major);
    return info;
}

int ProductInfo::year() const {
    switch (family) {
        case ProductFamily::V2022: return 2022;
        case ProductFamily::V2023: return 2023;
        case ProductFamily::V2024: return 2024;
        case ProductFamily::V2025: return 2025;
        case ProductFamily::V2026: return 2026;
        default: break;
    }
    // Legacy + "next" families still have a meaningful major -> year mapping
    // (13 -> 2019, 27 -> 2027) which is what the user sees in Premiere's
    // About box, so report that rather than 0.
    if (version_known && file_version.major >= 13) return file_version.major + 2000;
    return 0;
}

std::string ProductInfo::display_name() const {
    const bool beta = channel == ProductChannel::Beta;
    const int y = year();
    if (y == 0) {
        return beta ? "Premiere Pro Beta (unknown version)" : "Premiere Pro (unknown version)";
    }
    std::string name = str_format("Premiere Pro %d", y);
    if (beta) name += " Beta";
    return name;
}

std::string ProductInfo::version_string() const {
    return version_known ? format_version(file_version) : std::string("unknown");
}

std::string ProductInfo::family_key() const {
    std::string key = product_family_name(family);
    if (channel == ProductChannel::Beta) key += "-beta";
    return key;
}

}  // namespace azy
