#include "azy/core/compat.hpp"
#include <string>

#include "azy/core/strings.hpp"

namespace azy {
namespace {

void note(FeatureSet& fs, const char* why) { fs.reasons.push_back(why); }

}  // namespace

FeatureSet resolve_features(const ProductInfo& product,
                            const HostCapabilities& host,
                            bool explicit_safe_mode,
                            bool performance_mode,
                            bool experimental_opt_in) {
    FeatureSet fs;

    // --- Host gating -------------------------------------------------------
    // Level 1 (window composition) is only meaningful while DWM is composing.
    fs.dark_frame = host.dark_titlebar;
    if (!host.dark_titlebar) note(fs, "host does not support dark title bars");

    fs.frame_colors = host.frame_colors;
    if (!host.frame_colors) note(fs, "host does not support window frame colors");

    fs.rounded_frame = host.rounded_corners;
    if (!host.rounded_corners) note(fs, "host does not support rounded window frames");

    // Backdrop composition on a foreign, fully opaque client window only ever
    // shows through the non-client frame, and only on Windows 11 22H2+. It is
    // therefore treated as an experimental feature rather than a default.
    fs.frame_backdrop = host.system_backdrop && experimental_opt_in;
    if (!host.system_backdrop) note(fs, "host does not support system backdrop");

    fs.edge_surface = host.layered_windows && host.win_event_hooks;

    // --- Premiere build gating --------------------------------------------
    // Unknown builds (unreadable VERSIONINFO, or a version newer than this
    // release of Azy Skin) get the conservative treatment: no window-frame
    // recolouring, and a single hairline edge surface with no rounding.
    switch (product.family) {
        case ProductFamily::V2026:
        case ProductFamily::V2025:
        case ProductFamily::V2024:
            break;  // fully supported
        case ProductFamily::VNext:
            note(fs, "Premiere build newer than this Azy Skin release");
            fs.frame_colors = false;
            fs.rounded_frame = false;
            break;
        case ProductFamily::V2023:
        case ProductFamily::V2022:
            note(fs, "older Premiere build: reduced treatment");
            fs.rounded_frame = false;
            break;
        case ProductFamily::Legacy:
            note(fs, "legacy Premiere build: conservative treatment");
            fs.frame_colors = false;
            fs.rounded_frame = false;
            fs.frame_backdrop = false;
            break;
        case ProductFamily::Unknown:
        default:
            note(fs, "unknown Premiere version: conservative treatment");
            fs.frame_colors = false;
            fs.rounded_frame = false;
            fs.frame_backdrop = false;
            fs.experimental = false;
            break;
    }

    // Beta builds are a separate executable and can lag or lead the release
    // build's window structure; keep them conservative too.
    if (product.channel == ProductChannel::Beta) {
        note(fs, "Beta channel: reduced treatment");
        fs.rounded_frame = false;
    }

    // --- User policy -------------------------------------------------------
    if (performance_mode) {
        // Performance mode: static colours only. No shadows, no translucency,
        // nothing that needs per-pixel alpha composition.
        fs.frame_backdrop = false;
        fs.edge_surface_rounded = false;
        note(fs, "performance mode");
    }

    if (explicit_safe_mode) {
        // Safe mode is deliberately narrow: one documented DWM attribute that
        // every supported Windows build accepts, nothing of our own on screen.
        note(fs, "safe mode");
        fs.frame_colors = false;
        fs.rounded_frame = false;
        fs.frame_backdrop = false;
        fs.edge_surface = false;
        fs.edge_surface_rounded = false;
        fs.experimental = false;
    }

    fs.experimental = fs.experimental || (experimental_opt_in && product.family != ProductFamily::Unknown);
    if (!experimental_opt_in) {
        fs.frame_backdrop = false;
    }
    return fs;
}

std::string feature_summary(const FeatureSet& features) {
    if (!features.edge_surface && !features.frame_colors && features.dark_frame) {
        return "safe mode";
    }
    if (!features.edge_surface && !features.dark_frame) return "disabled";
    if (features.frame_backdrop) return "full + experimental";
    if (features.rounded_frame && features.frame_colors && features.edge_surface) return "full";
    if (features.reasons.size() >= 2) return "reduced";
    return "standard";
}

}  // namespace azy
