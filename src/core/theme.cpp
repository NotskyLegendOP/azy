#include "azy/core/theme.hpp"

#include <algorithm>
#include <cmath>

namespace azy {
namespace {

double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

unsigned char to_byte(double v) {
    const double c = clamp01(v / 255.0) * 255.0;
    return static_cast<unsigned char>(c + 0.5);
}

}  // namespace

Rgba mix_color(Rgba a, Rgba b, double t) {
    const double k = clamp01(t);
    Rgba out;
    out.r = to_byte(a.r * (1.0 - k) + b.r * k);
    out.g = to_byte(a.g * (1.0 - k) + b.g * k);
    out.b = to_byte(a.b * (1.0 - k) + b.b * k);
    out.a = to_byte(a.a * (1.0 - k) + b.a * k);
    return out;
}

Rgba with_alpha(Rgba c, double alpha01) {
    c.a = to_byte(clamp01(alpha01) * 255.0);
    return c;
}

}  // namespace azy
