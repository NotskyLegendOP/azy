// Azy Skin — portable core: geometry / DPI helpers.
//
// All numbers inside Azy are physical pixels on the virtual desktop. Logical
// sizes (the ones a user thinks in, e.g. "8px corner radius") are converted
// through the DPI of the monitor the Premiere window is actually on, which is
// what keeps 125%/150%/200% hosts pixel-exact instead of blurry.
#pragma once

#include <algorithm>
#include <cmath>

namespace azy {

constexpr int kBaseDpi = 96;

inline double dpi_scale(int dpi) {
    if (dpi <= 0) return 1.0;
    return static_cast<double>(dpi) / static_cast<double>(kBaseDpi);
}

inline int dip_to_px(double dip, int dpi) {
    const double px = dip * dpi_scale(dpi);
    return static_cast<int>(px < 0 ? px - 0.5 : px + 0.5);
}

inline double px_to_dip(int px, int dpi) {
    const double s = dpi_scale(dpi);
    return s <= 0.0 ? px : px / s;
}

inline int clamp_int(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct Rect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    int width() const { return right - left; }
    int height() const { return bottom - top; }
    bool empty() const { return right <= left || bottom <= top; }

    static Rect from_size(int x, int y, int w, int h) {
        Rect r;
        r.left = x;
        r.top = y;
        r.right = x + (w > 0 ? w : 0);
        r.bottom = y + (h > 0 ? h : 0);
        return r;
    }
};

inline bool operator==(const Rect& a, const Rect& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}
inline bool operator!=(const Rect& a, const Rect& b) { return !(a == b); }

// True when the two rectangles differ by more than `tolerance` physical pixels
// on any edge. Used to ignore the 1px churn Windows produces while a window is
// being dragged, so the skin does not rebuild its surface for noise.
inline bool rect_changed(const Rect& a, const Rect& b, int tolerance = 0) {
    const int t = tolerance < 0 ? 0 : tolerance;
    return std::abs(a.left - b.left) > t || std::abs(a.top - b.top) > t ||
           std::abs(a.right - b.right) > t || std::abs(a.bottom - b.bottom) > t;
}

// Intersection of two rectangles; an empty rect when they do not overlap.
inline Rect intersect_rect(const Rect& a, const Rect& b) {
    Rect r;
    r.left = (a.left > b.left ? a.left : b.left);
    r.top = (a.top > b.top ? a.top : b.top);
    r.right = (a.right < b.right ? a.right : b.right);
    r.bottom = (a.bottom < b.bottom ? a.bottom : b.bottom);
    if (r.right < r.left) r.right = r.left;
    if (r.bottom < r.top) r.bottom = r.top;
    return r;
}

}  // namespace azy
