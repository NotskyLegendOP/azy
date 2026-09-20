// Azy Skin — Win32 layer: SkinEngine.
//
// The engine owns both composition levels and decides when either of them has to
// do anything. It keeps a "visual key" of what is currently on screen; when the
// incoming request describes the same state, `apply()` returns false immediately
// without a single DWM call or repaint. That is what makes a static Premiere
// window cost nothing.
#pragma once

#include <string>

#include "azy/win32/skin/composition_surface.hpp"
#include "azy/win32/skin/dwm_composer.hpp"
#include "azy/win32/skin/skin_types.hpp"

namespace azy {
namespace win {

class SkinEngine {
public:
    struct VisualKey {
        HWND hwnd = nullptr;

        // Level 1 (window frame)
        bool dark_frame = false;
        bool frame_colors = false;
        bool rounded_frame = false;
        bool frame_backdrop = false;
        Rgba frame_caption;
        Rgba frame_border;
        Rgba frame_text;

        // Level 2 (composition surface)
        bool surface = false;
        Rect surface_rect;
        UINT dpi = 96;
        int radius_px = 0;
        int band_px = 0;
        bool shadow = false;
        bool glass = false;
        Rgba fill;
        Rgba border;
        Rgba highlight;
        Rgba shadow_color;

        bool operator==(const VisualKey& other) const;
    };

    ~SkinEngine() { shutdown(); }

    bool initialize(std::string* error);
    void shutdown();

    // Applies (or removes) the skin for this request. Returns true when the
    // on-screen result actually changed.
    bool apply(const SkinRequest& request, SkinState& state_out);

    // Full teardown: restores the frame and hides the surface.
    void revert();

    // Destroys the composition surface window and its bitmap. Called when
    // Premiere exits, so Azy holds no GDI/DWM resources while nothing is being
    // skinned.
    void release_surface();

    bool frame_applied() const { return composer_.is_applied(); }
    bool surface_visible() const { return surface_.visible(); }
    HWND surface_window() const { return surface_.hwnd(); }
    const VisualKey& last_key() const { return last_key_; }
    bool has_key() const { return has_key_; }
    unsigned long long surface_presents() const { return surface_.presents(); }

private:
    VisualKey build_key(const SkinRequest& request) const;

    DwmComposer composer_;
    CompositionSurface surface_;
    VisualKey last_key_;
    bool has_key_ = false;
};

}  // namespace win
}  // namespace azy
