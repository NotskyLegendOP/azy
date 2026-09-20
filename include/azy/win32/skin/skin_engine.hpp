// Azy Skin — Win32 layer: SkinEngine.
//
// The engine owns both composition levels and decides when either of them has to
// do anything. It keeps a "visual key" of what is currently on screen; when the
// incoming request describes the same state, `apply()` returns false immediately
// without a single DWM call or repaint. That is what makes a static Premiere
// window cost nothing.
#pragma once

#include <string>

#include "azy/core/panel_map.hpp"
#include "azy/win32/skin/composition_surface.hpp"
#include "azy/win32/skin/debug_overlay.hpp"
#include "azy/win32/skin/dwm_composer.hpp"
#include "azy/win32/skin/overlay_veil.hpp"
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
        Rgba bezel;
        Rgba fill;
        Rgba border;
        Rgba highlight;
        Rgba shadow_color;

        // Whole-window overlay (veil).
        bool overlay = false;
        Rgba veil;

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

    // Forces the next apply() to rebuild everything (used after the user asks for a
    // fresh look at the screen: nothing has changed, so nothing would be presented).
    void invalidate() { has_key_ = false; }

    // Puts the surfaces back in front of Premiere when something raised it above
    // them - activating Premiere is enough, because Azy's surfaces are ordinary
    // windows and an active window goes to the top of the band. A z-order walk and,
    // only when the order is actually wrong, one SetWindowPos per surface - so the
    // usual call (window events, and the low-frequency settle timer) costs a few
    // GetWindow calls and nothing else.
    void reassert_stacking();

    // Checks with the desktop itself that the skin is really reaching the screen:
    // the overlay first (it covers everything, so it is the easiest to detect),
    // then the ring. `detail` always describes what was measured, so the answer can
    // be pasted into a bug report. Returns true when at least one layer is on
    // screen; the detail says which.
    bool probe_on_screen(std::string* detail);
    bool overlay_visible() const { return veil_.visible(); }
    bool debug_visible() const { return debug_.visible(); }
    // The panel map of the last apply: what Azy currently believes about the
    // tracked window's layout (spec §41 shows it; the region work will draw with
    // it). Empty until a window is attached.
    const std::vector<PanelRect>& panel_map() const { return panels_; }
    // Where the panel map's client area sits on screen (its origin), so a caller
    // can translate panel rectangles into screen coordinates.
    Rect client_origin() const { return client_origin_; }
    unsigned char overlay_alpha() const { return veil_.alpha(); }
    const RingReport& ring_report() const { return surface_.report(); }
    HWND surface_window() const { return surface_.hwnd(); }
    const VisualKey& last_key() const { return last_key_; }
    bool has_key() const { return has_key_; }
    unsigned long long surface_presents() const { return surface_.presents(); }

private:
    VisualKey build_key(const SkinRequest& request) const;

    DwmComposer composer_;
    CompositionSurface surface_;
    OverlayVeil veil_;
    DebugOverlay debug_;
    std::vector<PanelRect> panels_;
    Rect client_origin_;
    HWND target_ = nullptr;  // the Premiere window the surfaces were placed against
    VisualKey last_key_;
    bool has_key_ = false;
};

}  // namespace win
}  // namespace azy
