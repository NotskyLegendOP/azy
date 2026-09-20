// Azy Skin — portable core: user settings + INI (de)serialisation.
//
// The file is a small human-editable INI under
// %LOCALAPPDATA%\Azy Skin\settings.ini. Unknown keys are preserved on write
// and ignored on read, so a newer/older build never destroys a config.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "azy/core/panel_map.hpp"
#include "azy/core/theme.hpp"
#include "azy/core/version.hpp"

namespace azy {

// Feature keys that can be individually disabled from the Advanced page.
namespace feature_key {
constexpr const char* kFrameColors = "frame_colors";
constexpr const char* kRoundedFrame = "rounded_frame";
constexpr const char* kFrameBackdrop = "frame_backdrop";
constexpr const char* kEdgeSurface = "edge_surface";
constexpr const char* kRoundedSurface = "rounded_surface";
constexpr const char* kShadow = "shadow";
constexpr const char* kGlass = "glass";
constexpr const char* kOverlay = "window_overlay";
}  // namespace feature_key

struct Settings {
    int schema = 1;

    // --- Skin ---
    bool enabled = true;               // master ON/OFF (tray + settings window)
    bool start_with_windows = false;   // HKCU Run entry
    bool apply_automatically = true;   // skin Premiere as soon as it appears

    // --- Appearance ---
    Appearance appearance;

    // --- Performance ---
    bool performance_mode = false;     // static colours only, minimum monitoring
    bool suspend_when_minimized = true;
    bool suspend_when_inactive = false;

    // --- Advanced / Compatibility ---
    bool experimental = false;         // opt-in to features that need verification
    bool safe_mode = false;
    // Debug mode (spec §41): draw the panel map and the facts around it over the
    // tracked window. A developer aid, off by default, costs nothing while off.
    bool debug_mode = false;
    // Which layout the panel model should assume (spec §39 "manual UI profile").
    // `Auto` means "not chosen": the model uses the Editing layout.
    WorkspaceId ui_profile = WorkspaceId::Auto;
    // Quality preset last applied, kept so the settings UI can show `Custom`
    // honestly across restarts. The individual appearance values are always
    // authoritative; this is only what the combo displays.
    PresetId preset = PresetId::Balanced;
    std::string safe_mode_reason;

    // Failure tracker state (persisted so repeated crashes across restarts
    // still lead to Safe Mode).
    int failure_count = 0;
    double failure_window_start = 0.0;
    double failure_last = 0.0;
    bool failure_tripped = false;

    // Diagnostics
    std::string last_premiere_version;
    std::map<std::string, std::string> extra;  // unknown keys, round-tripped

    // Clamps every value into its supported range. Always called after load.
    void clamp();

    // Applies one of the four quality presets (spec §40) over the appearance and
    // the performance switch. Balanced is the shipped default look.
    void apply_preset(PresetId preset);
    // Which preset the current values correspond to, or `Custom` when the user has
    // moved something since. Used by the settings UI so the combo never lies.
    PresetId current_preset() const;

    // "key,key2" list of features the user disabled in Advanced.
    bool feature_disabled(const std::string& key) const;
    void set_feature_disabled(const std::string& key, bool disabled);
    std::string disabled_feature_list() const;

    std::string to_ini() const;
    static Settings from_ini(const std::string& text, std::vector<std::string>* warnings = nullptr);
};

}  // namespace azy
