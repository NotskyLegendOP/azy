// Azy Skin — portable core: per-Premiere-version compatibility rules.
//
// Premiere Pro renders almost all of its interface itself; the only handles
// Windows gives an external process are documented DWM/window-composition
// attributes and our own top-level composition surfaces. Those handles were
// added to Windows over several releases, so what Azy Skin is allowed to do
// depends on BOTH the Windows build and the Premiere build.
//
// This file is the single place where that policy lives, so a future Premiere
// update can be handled by editing a table instead of touching engine code.
#pragma once

#include <string>
#include <vector>

#include "azy/core/product.hpp"

namespace azy {

// What the host Windows build can do. Resolved at runtime (see
// src/win32/os/win_version.*) but modelled here so the policy is testable.
struct HostCapabilities {
    int windows_build = 0;  // e.g. 19045 (Win10 22H2), 22621 (Win11 22H2)
    bool dwm_composition = false;      // Desktop Window Manager composing the desktop
    bool dark_titlebar = false;        // DWMWA_USE_IMMERSIVE_DARK_MODE (Win10 1809+)
    bool frame_colors = false;         // DWMWA_CAPTION/BORDER/TEXT_COLOR (Win11 22000+)
    bool rounded_corners = false;      // DWMWA_WINDOW_CORNER_PREFERENCE (Win11 22000+)
    bool system_backdrop = false;      // DWMWA_SYSTEMBACKDROP_TYPE (Win11 22621+)
    bool layered_windows = true;       // UpdateLayeredWindow (all supported Windows)
    bool win_event_hooks = true;       // SetWinEventHook (all supported Windows)
};

// Features the skin engine can switch on for a given Premiere build.
// Everything is opt-out: an unknown build simply gets fewer of them.
struct FeatureSet {
    bool dark_frame = true;        // cheap, reversible: dark caption/title bar
    bool frame_colors = true;      // charcoal caption + hairline border color
    bool rounded_frame = false;    // rounded window frame corners
    bool frame_backdrop = false;   // DWM backdrop (Mica/Acrylic) in the frame area
    bool edge_surface = true;      // Azy's own 1px border + soft inner shadow ring
    bool edge_surface_rounded = true;  // rounded ring corners (cheap, GDI-free)
    bool window_overlay = true;    // translucent sheet over the whole window
    bool experimental = false;     // anything gated behind Advanced settings

    // Human-readable list of what was disabled, for the log and settings UI.
    std::vector<std::string> reasons;
};

// Resolves the safe feature set for a Premiere build on this host.
// `explicit_safe_mode` (user setting or automatic safe mode) forces the
// minimum: dark frame only, no composition surfaces at all.
FeatureSet resolve_features(const ProductInfo& product,
                            const HostCapabilities& host,
                            bool explicit_safe_mode,
                            bool performance_mode,
                            bool experimental_opt_in);

// Short label used in the log + settings window:
// "full", "reduced (unknown version)", "safe mode", "performance mode".
std::string feature_summary(const FeatureSet& features);

}  // namespace azy
