#include "azy/win32/skin/gdiplus_renderer.hpp"
#include <algorithm>
#include <string>

#include <objidl.h>

#include <cmath>

#include <gdiplus.h>

#include "azy/core/log.hpp"
#include "azy/core/strings.hpp"
#include "azy/win32/os/win_util.hpp"

namespace azy {
namespace win {
namespace {

ULONG_PTR g_gdiplus_token = 0;
bool g_gdiplus_active = false;

Gdiplus::Color to_color(const Rgba& c) { return Gdiplus::Color(c.a, c.r, c.g, c.b); }

Rgba scaled_alpha(Rgba color, double factor) {
    const double alpha = std::max(0.0, std::min(1.0, factor)) * color.a;
    color.a = static_cast<unsigned char>(alpha + 0.5);
    return color;
}

// Rounded rectangle path. `radius` == 0 produces a plain rectangle.
void add_rounded_rect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect, float radius) {
    path.Reset();
    const float max_radius = std::min(rect.Width, rect.Height) * 0.5f;
    const float r = std::max(0.0f, std::min(radius, max_radius));
    if (r <= 0.01f) {
        path.AddRectangle(rect);
        return;
    }
    const float diameter = r * 2.0f;
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(rect.X + rect.Width - diameter, rect.Y, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(rect.X + rect.Width - diameter, rect.Y + rect.Height - diameter, diameter, diameter, 0.0f, 90.0f);
    path.AddArc(rect.X, rect.Y + rect.Height - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();
}

// One 1px stroke of the vignette, at a given inward depth from the frame edge.
void stroke_band(Gdiplus::Graphics& graphics, int width, int height, float radius, float depth,
                 const Gdiplus::Color& color, float thickness) {
    if (color.GetA() == 0) return;
    const float inset = depth + thickness * 0.5f;
    const float band_width = static_cast<float>(width) - 2.0f * inset;
    const float band_height = static_cast<float>(height) - 2.0f * inset;
    if (band_width <= 0.5f || band_height <= 0.5f) return;

    Gdiplus::GraphicsPath path;
    const Gdiplus::RectF rect(inset, inset, band_width, band_height);
    // The radius shrinks as the stroke moves inward, so the corners stay
    // concentric with the frame instead of bulging.
    add_rounded_rect(path, rect, std::max(0.0f, radius - inset));
    Gdiplus::Pen pen(color, thickness);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    graphics.DrawPath(&pen, &path);
}

}  // namespace

bool GdiPlusSession::start(std::string* error) {
    if (g_gdiplus_active) return true;
    Gdiplus::GdiplusStartupInput input;
    const Gdiplus::Status status = Gdiplus::GdiplusStartup(&g_gdiplus_token, &input, nullptr);
    if (status != Gdiplus::Ok) {
        if (error) *error = str_format("GdiplusStartup failed (status %d)", static_cast<int>(status));
        return false;
    }
    g_gdiplus_active = true;
    return true;
}

void GdiPlusSession::stop() {
    if (!g_gdiplus_active) return;
    Gdiplus::GdiplusShutdown(g_gdiplus_token);
    g_gdiplus_token = 0;
    g_gdiplus_active = false;
}

bool GdiPlusSession::active() { return g_gdiplus_active; }

void GdiPlusRenderer::release() {
    // Order matters, and it is not cosmetic. The GDI+ Bitmap wraps `bitmap_`, so it
    // goes first; the DIB is still selected into the memory DC, so the original
    // object has to go back before either can be released. Deleting a DC that has
    // a bitmap selected into it fails - and deleting the bitmap while it is
    // selected fails too - so the old order leaked one device context and one
    // DIB section on every resize, every DPI change and every monitor move, until
    // the process ran out of GDI handles and the ring stopped being drawn.
    if (graphics_bitmap_ != nullptr) {
        delete static_cast<Gdiplus::Bitmap*>(graphics_bitmap_);
        graphics_bitmap_ = nullptr;
    }
    if (memory_dc_ != nullptr) {
        if (previous_bitmap_ != nullptr) {
            SelectObject(memory_dc_, previous_bitmap_);
        }
        DeleteDC(memory_dc_);
        memory_dc_ = nullptr;
    }
    previous_bitmap_ = nullptr;
    if (bitmap_ != nullptr) {
        DeleteObject(bitmap_);
        bitmap_ = nullptr;
    }
    bits_ = nullptr;
    width_ = 0;
    height_ = 0;
}

bool GdiPlusRenderer::ensure_size(int width, int height, std::string* error) {
    if (width <= 0 || height <= 0) {
        if (error) *error = "invalid surface size";
        return false;
    }
    if (width == width_ && height == height_ && bitmap_ != nullptr) return true;

    release();

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;  // top-down: row 0 is the top edge
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(nullptr);
    if (screen == nullptr) {
        if (error) *error = "GetDC failed";
        return false;
    }
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    memory_dc_ = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);

    if (bitmap == nullptr || bits == nullptr || memory_dc_ == nullptr) {
        if (bitmap) DeleteObject(bitmap);
        if (memory_dc_) {
            DeleteDC(memory_dc_);
            memory_dc_ = nullptr;
        }
        if (error) *error = "CreateDIBSection failed";
        return false;
    }

    HGDIOBJ previous = SelectObject(memory_dc_, bitmap);
    if (previous == nullptr || previous == HGDI_ERROR) {
        DeleteObject(bitmap);
        DeleteDC(memory_dc_);
        memory_dc_ = nullptr;
        if (error) *error = "SelectObject failed";
        return false;
    }
    // Remembered so release() can put it back (see the note there).
    previous_bitmap_ = previous;

    // Premultiplied ARGB is what UpdateLayeredWindow expects (AC_SRC_ALPHA).
    auto* gdi_bitmap =
        new Gdiplus::Bitmap(width, height, width * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(bits));
    if (gdi_bitmap->GetLastStatus() != Gdiplus::Ok) {
        delete gdi_bitmap;
        // The DIB is selected into the DC by now: put the old object back first, or
        // neither handle can be released (the leak this fix exists for).
        SelectObject(memory_dc_, previous_bitmap_);
        previous_bitmap_ = nullptr;
        DeleteObject(bitmap);
        DeleteDC(memory_dc_);
        memory_dc_ = nullptr;
        if (error) *error = "GDI+ Bitmap creation failed";
        return false;
    }

    bitmap_ = bitmap;
    bits_ = bits;
    graphics_bitmap_ = gdi_bitmap;
    width_ = width;
    height_ = height;
    return true;
}

bool GdiPlusRenderer::prepare(int width, int height, std::string* error) {
    if (!g_gdiplus_active) {
        if (error) *error = "GDI+ session is not running";
        return false;
    }
    return ensure_size(width, height, error);
}

bool GdiPlusRenderer::render(const RingVisual& visual, std::string* error) {    if (!g_gdiplus_active) {
        if (error) *error = "GDI+ session is not running";
        return false;
    }
    // Defensive: paint the whole frame when the caller did not size the bitmap.
    if (!valid() && !ensure_size(visual.width_px, visual.height_px, error)) return false;

    auto* bitmap = static_cast<Gdiplus::Bitmap*>(graphics_bitmap_);
    Gdiplus::Graphics graphics(bitmap);
    if (graphics.GetLastStatus() != Gdiplus::Ok) {
        if (error) *error = "GDI+ Graphics creation failed";
        return false;
    }
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));

    // Draw in frame coordinates: the strip's bitmap receives the part of the ring
    // that falls inside it.
    graphics.TranslateTransform(static_cast<Gdiplus::REAL>(-visual.origin_x),
                                static_cast<Gdiplus::REAL>(-visual.origin_y));

    const int width = visual.width_px;
    const int height = visual.height_px;
    const int band = std::max(1, visual.band_px);
    const float radius = static_cast<float>(visual.radius_px);

    // The wash fades out within a few pixels of the edge, so it reads as depth
    // and never as a solid strip drawn over the interface.
    const int wash_depth = std::max(2, std::min(band, 6));

    // Per-pixel-depth strokes. The loop count is bounded by the band thickness
    // (about 10-20px, and 1-3px in performance mode), never by the window size, so
    // a 4K window costs exactly the same as a small dialog.
    for (int depth = band + 1; depth >= 1; --depth) {
        if (depth == 1) {
            // Outermost pixel inside the hairline: the raised bezel. This is what
            // makes the frame edge legible over Premiere's near-black panels.
            if (visual.bezel.a > 0) {
                stroke_band(graphics, width, height, radius, 0.0f, to_color(visual.bezel), 1.0f);
            }
            continue;
        }

        // Quadratic falloff reads as a soft shadow rather than a hard band, and it
        // starts one pixel in so it never muddies the bezel.
        const double offset = static_cast<double>(depth - 2) / static_cast<double>(band);
        const double edge_strength = 1.0 - offset;
        double strength = visual.draw_shadow ? edge_strength * edge_strength : 0.0;

        Rgba color = visual.shadow;
        if (strength > 0.0) color = scaled_alpha(color, strength);

        if (visual.draw_fill) {
            const double wash = depth <= wash_depth
                                    ? 1.0 - static_cast<double>(depth - 1) / static_cast<double>(wash_depth)
                                    : 0.0;
            if (wash > 0.0) {
                const Rgba wash_color = scaled_alpha(visual.fill, wash * wash);
                const double total = static_cast<double>(color.a) + wash_color.a;
                if (total > 0.0) {
                    const double mix = wash_color.a / total;
                    color.r = static_cast<unsigned char>(color.r * (1.0 - mix) + wash_color.r * mix);
                    color.g = static_cast<unsigned char>(color.g * (1.0 - mix) + wash_color.g * mix);
                    color.b = static_cast<unsigned char>(color.b * (1.0 - mix) + wash_color.b * mix);
                    color.a = static_cast<unsigned char>(total > 255.0 ? 255 : total);
                }
            }
        }
        if (color.a == 0) continue;

        stroke_band(graphics, width, height, radius, static_cast<float>(depth - 2), to_color(color), 1.0f);
    }

    // Hairline on the frame edge itself, drawn last so it is always crisp.
    if (visual.draw_border && visual.border.a > 0) {
        stroke_band(graphics, width, height, radius, 0.0f, to_color(visual.border), 1.0f);
    }

    // Barely visible top inner highlight: light falling on glass, not a glow.
    if (visual.draw_highlight && visual.highlight.a > 0 && width > 4 * radius + 8) {
        const float left = radius > 0 ? radius + 2.0f : 2.0f;
        const float right = static_cast<float>(width) - left;
        const float y = static_cast<float>(band) + 0.5f;
        if (right > left) {
            Gdiplus::Pen pen(to_color(visual.highlight), 1.0f);
            graphics.DrawLine(&pen, left, y, right, y);
        }
    }

    if (graphics.GetLastStatus() != Gdiplus::Ok) {
        if (error) *error = "GDI+ draw call reported an error";
        return false;
    }
    graphics.Flush(Gdiplus::FlushIntentionSync);
    return true;
}

unsigned char GdiPlusRenderer::max_alpha() const {
    if (bits_ == nullptr || width_ <= 0 || height_ <= 0) return 0;
    const auto* bytes = static_cast<const unsigned char*>(bits_);
    const size_t pixels = static_cast<size_t>(width_) * static_cast<size_t>(height_);
    unsigned char strongest = 0;
    for (size_t i = 0; i < pixels; ++i) {
        const unsigned char alpha = bytes[i * 4u + 3u];
        if (alpha > strongest) strongest = alpha;
    }
    return strongest;
}

}  // namespace win
}  // namespace azy
