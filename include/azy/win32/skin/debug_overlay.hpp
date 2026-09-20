// Azy Skin — Win32 layer: the debug overlay (spec §41).
//
// A developer/debug mode that shows what Azy believes about the window it is
// decorating: the panel rectangles its model produced, the window handle, the
// DPI, the monitor and the workspace. The point is not decoration - it is that a
// user can take one screenshot and the panel model can be corrected from it.
//
// It follows the same rules as every other Azy surface: one click-through,
// non-activating, tool-window layer, drawn once per change (never per frame), and
// destroyed with the attachment. It is the only surface Azy draws that contains
// text, and it exists only while debug mode is switched on, so it costs nothing
// in normal use.
#pragma once

#include <string>
#include <vector>

#include "azy/core/geometry.hpp"
#include "azy/core/panel_map.hpp"
#include "azy/win32/os/win_compat.hpp"

namespace azy {
namespace win {

class DebugOverlay {
public:
    DebugOverlay() = default;
    ~DebugOverlay() { destroy(); }

    DebugOverlay(const DebugOverlay&) = delete;
    DebugOverlay& operator=(const DebugOverlay&) = delete;

    // Shows the panel map and the facts around it, above `below` (Premiere). The
    // panel rectangles are in *screen* coordinates (the map is built for the
    // client area, then offset), because the overlay window covers the same area.
    bool present(HWND below, HWND ring_strip, const Rect& frame, unsigned dpi,
                 const std::vector<PanelRect>& panels, const std::vector<std::string>& facts,
                 std::string* error);

    void hide();
    void destroy();

    bool visible() const { return visible_; }
    HWND hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    bool ensure_created(std::string* error);
    void ensure_font(unsigned dpi);
    void release_font();
    void paint_now();
    bool same_content(const std::vector<PanelRect>& panels, const std::vector<std::string>& facts) const;

    HWND hwnd_ = nullptr;
    std::wstring class_name_;
    Rect rect_;
    // Where this window's own client origin sits on screen. The caller hands over
    // rectangles in screen coordinates; painting happens in window coordinates, so
    // the difference has to be removed somewhere - exactly once, here.
    Rect origin_;
    HFONT font_ = nullptr;
    int font_dpi_ = 0;
    std::vector<PanelRect> panels_;
    std::vector<std::string> facts_;
    bool visible_ = false;
};

}  // namespace win
}  // namespace azy
