#include "azy/core/capture_math.hpp"

#include <algorithm>
#include <cstddef>

namespace azy {
namespace {

inline float fraction(int value, int origin, int size) {
    if (size <= 0) return 0.0f;
    return static_cast<float>(value - origin) / static_cast<float>(size);
}

inline float clamp01(float value) { return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); }

// Panel ids that carry picture and must be left alone, in the order they are
// handed to the shader.
bool is_picture_panel(PanelId id) {
    return id == PanelId::ProgramMonitor || id == PanelId::SourceMonitor;
}

bool intersects(const LocalRect& a, const LocalRect& b) {
    return !a.empty() && !b.empty() && a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

// True when the two rectangles share an edge closely enough that drawing one of
// them would paint over the other's border.
bool touches(const LocalRect& a, const LocalRect& b) {
    const float tolerance = 1.5f;
    const bool horizontal = (a.right >= b.left - tolerance && a.right <= b.left + tolerance) ||
                            (b.right >= a.left - tolerance && b.right <= a.left + tolerance);
    const bool vertical = (a.bottom >= b.top - tolerance && a.bottom <= b.top + tolerance) ||
                          (b.bottom >= a.top - tolerance && b.bottom <= a.top + tolerance);
    const bool overlaps_y = a.top < b.bottom && b.top < a.bottom;
    const bool overlaps_x = a.left < b.right && b.left < a.right;
    return (horizontal && overlaps_y) || (vertical && overlaps_x);
}

}  // namespace

UvRect map_overlay_to_capture(const Rect& overlay, const Rect& captured) {
    UvRect uv;
    if (captured.empty() || overlay.empty()) return uv;

    const int width = captured.width();
    const int height = captured.height();
    if (width <= 0 || height <= 0) return uv;

    // Fractions, not pixels: a capture texture can be a pixel smaller or larger
    // than the rectangle Windows reports for the window, and a DPI change moves
    // both without changing their relationship.
    uv.u0 = clamp01(fraction(overlay.left, captured.left, width));
    uv.v0 = clamp01(fraction(overlay.top, captured.top, height));
    uv.u1 = clamp01(fraction(overlay.right, captured.left, width));
    uv.v1 = clamp01(fraction(overlay.bottom, captured.top, height));

    // No intersection at all (or a fully clamped, empty crop) means the overlay
    // and the captured window do not describe the same thing right now. Drawing
    // something anyway would stretch the whole window into whatever rectangle we
    // happen to have, which is worse than showing nothing.
    if (uv.u1 - uv.u0 < 0.02f || uv.v1 - uv.v0 < 0.02f) {
        return UvRect{};
    }
    if (overlay.right <= captured.left || overlay.left >= captured.right || overlay.bottom <= captured.top ||
        overlay.top >= captured.bottom) {
        return UvRect{};
    }

    uv.valid = true;
    return uv;
}

LocalRect clip_to_overlay(const Rect& source, const Rect& overlay) {
    LocalRect out;
    if (overlay.empty() || source.empty()) return out;

    const int left = std::max(source.left, overlay.left);
    const int top = std::max(source.top, overlay.top);
    const int right = std::min(source.right, overlay.right);
    const int bottom = std::min(source.bottom, overlay.bottom);
    if (right - left <= 0 || bottom - top <= 0) return out;

    out.left = static_cast<float>(left - overlay.left);
    out.top = static_cast<float>(top - overlay.top);
    out.right = static_cast<float>(right - overlay.left);
    out.bottom = static_cast<float>(bottom - overlay.top);
    return out;
}

Rect overlay_rect(const Rect& visible_frame, const Rect& monitor, const Rect& work_area, bool maximized,
                  bool fullscreen) {
    if (visible_frame.empty()) return Rect{};
    const Rect band = fullscreen ? monitor : (maximized ? work_area : visible_frame);
    if (band.empty()) return visible_frame;
    const Rect overlap = intersect_rect(visible_frame, band);
    // A window that is entirely outside its own monitor band (mid-move between
    // displays) keeps its own rectangle: showing the duplicate where the window is
    // beats showing it nowhere while the geometry settles.
    return overlap.empty() ? visible_frame : overlap;
}

std::vector<LocalRect> monitor_pass_through(const std::vector<PanelRect>& panels, const Rect& overlay,
                                            const Rect& client_origin) {
    std::vector<LocalRect> out;
    if (overlay.empty()) return out;

    // Two passes so the Program Monitor is always the first entry: a shader with
    // a fixed array must never spend its slots on less important rectangles.
    for (int pass = 0; pass < 2; ++pass) {
        for (const PanelRect& panel : panels) {
            if (!panel.usable) continue;
            const bool wanted = pass == 0 ? panel.id == PanelId::ProgramMonitor : is_picture_panel(panel.id) &&
                                                                                       panel.id !=
                                                                                           PanelId::ProgramMonitor;
            if (!wanted) continue;
            // The panel map describes the client area; the overlay covers the
            // window frame. Adding the client origin puts both in screen space.
            const Rect screen = Rect::from_size(panel.rect.left + client_origin.left,
                                                panel.rect.top + client_origin.top, panel.rect.width(),
                                                panel.rect.height());
            const LocalRect local = clip_to_overlay(screen, overlay);
            if (local.empty()) continue;
            // A sliver of a monitor is not worth a pass-through slot: leaving it
            // skinned costs nothing visually and keeps the shader loop short.
            if (local.right - local.left < 16.0f || local.bottom - local.top < 16.0f) continue;
            out.push_back(local);
        }
    }
    return out;
}

std::vector<LocalRect> panel_hairlines(const std::vector<PanelRect>& panels, const Rect& overlay,
                                       const Rect& client_origin, const std::vector<LocalRect>& exclude,
                                       std::size_t limit) {
    std::vector<LocalRect> out;
    if (overlay.empty() || limit == 0) return out;

    for (const PanelRect& panel : panels) {
        if (!panel.usable) continue;
        if (panel.id == PanelId::ProgramMonitor || panel.id == PanelId::SourceMonitor) continue;
        const Rect screen = Rect::from_size(panel.rect.left + client_origin.left,
                                            panel.rect.top + client_origin.top, panel.rect.width(),
                                            panel.rect.height());
        const LocalRect local = clip_to_overlay(screen, overlay);
        if (local.empty()) continue;
        // A hairline around a pass-through region would draw the skin *on* the
        // video edge, which is exactly what the pass-through exists to prevent.
        bool blocked = false;
        for (const LocalRect& region : exclude) {
            if (intersects(local, region) || touches(local, region)) {
                blocked = true;
                break;
            }
        }
        if (blocked) continue;
        out.push_back(local);
        if (out.size() >= limit) break;
    }
    return out;
}

void pack_rects(const std::vector<LocalRect>& rects, float* out, std::size_t slots) {
    if (out == nullptr || slots == 0) return;
    for (std::size_t i = 0; i < slots; ++i) {
        float* slot = out + i * 4;
        if (i < rects.size()) {
            slot[0] = rects[i].left;
            slot[1] = rects[i].top;
            slot[2] = rects[i].right;
            slot[3] = rects[i].bottom;
        } else {
            slot[0] = slot[1] = slot[2] = slot[3] = 0.0f;
        }
    }
}

}  // namespace azy
