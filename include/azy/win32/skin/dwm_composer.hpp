// Azy Skin — Win32 layer: DWM composer (Level 1).
//
// This is the most invasive thing Azy does, and it is deliberately the least
// invasive technique Windows offers for restyling another process' window:
// documented DwmSetWindowAttribute attributes that DWM applies to the window's
// non-client frame. No injection, no hooking, no code inside Premiere, and every
// attribute is restored to its previous value when the skin is turned off.
//
// If DWM rejects an attribute, nothing happens except a log line - the window
// keeps its original appearance, which is exactly the graceful fallback the
// design calls for.
#pragma once

#include <string>

#include "azy/core/compat.hpp"
#include "azy/core/theme.hpp"
#include "azy/win32/skin/skin_types.hpp"

namespace azy {
namespace win {

class DwmComposer {
public:
    struct ApplyResult {
        bool dark_frame = false;
        bool frame_colors = false;
        bool rounded_frame = false;
        bool backdrop = false;
        bool any_failed = false;
        std::string error;
    };

    ~DwmComposer() { revert(); }

    // Applies the frame treatment to `target`. Reverts a previously treated
    // window first when the target changed, so Azy never leaves two windows
    // modified.
    ApplyResult apply(const SkinTarget& target, const ThemePalette& palette, const FeatureSet& features,
                      bool maximized_or_fullscreen);

    // Restores the previous frame values (or the DWM defaults when the previous
    // value could not be read).
    void revert();
    bool is_applied() const { return applied_.hwnd != nullptr; }
    HWND applied_window() const { return applied_.hwnd; }
    bool rounded_applied() const { return applied_.rounded; }

private:
    struct FrameState {
        HWND hwnd = nullptr;
        bool dark = false;
        bool colors = false;
        bool rounded = false;
        bool backdrop = false;
        bool dark_saved = false;
        BOOL previous_dark = FALSE;
        bool corner_saved = false;
        int previous_corner = dwm_attr::kCornerDefault;
        bool caption_saved = false;
        DWORD previous_caption = 0;
        bool border_saved = false;
        DWORD previous_border = 0;
        bool text_saved = false;
        DWORD previous_text = 0;
        bool backdrop_saved = false;
        int previous_backdrop = dwm_attr::kBackdropAuto;
    };

    void reset_state();

    FrameState applied_;
};

// Reads the frame values DWM currently reports; used to guarantee a faithful
// revert. Returns false when the host does not support reading an attribute.
bool dwm_query_dark_mode(HWND hwnd, BOOL& out);
bool dwm_query_corner_preference(HWND hwnd, int& out);
bool dwm_query_color(HWND hwnd, DWORD attribute, DWORD& out);

}  // namespace win
}  // namespace azy
