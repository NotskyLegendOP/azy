#include "azy/win32/skin/gdiplus_renderer.hpp"

#include <objidl.h>

#include <algorithm>
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

std::string GdiPlusSession::version_string() {
    // The GDI+ version is in the registry; not worth a lookup for a log line.
    return "GDI+";
}

void GdiPlusRenderer::release() {
    if (graphics_bitmap_ != nullptr) {
        delete static_cast<Gdiplus::Bitmap*>(graphics_bitmap_);
        graphics_bitmap_ = nullptr;
    }
    if (memory_dc_ != nullptr) {
        DeleteDC(memory_dc_);
        memory_dc_ = nullptr;
    }
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

    // Premultiplied ARGB is what UpdateLayeredWindow expects (AC_SRC_ALPHA).
    auto* gdi_bitmap = new Gdiplus::Bitmap(width, height, width * 4, PixelFormat32bppPARGB,
                                           static_cast<BYTE*>(bits));
    if (gdi_bitmap->GetLastStatus() != Gdiplus::Ok) {
        delete gdi_bitmap;
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

bool GdiPlusRenderer::render(const RingVisual& visual, std::string* error) {
    if (!g_gdiplus_active) {
        if (error) *error = "GDI+ session is not running";
        return false;
    }
    if (!ensure_size(visual.width_px, visual.width_px, error)) return false;

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

    // Start from a fully transparent surface: we render the state we want, we do
    // not accumulate over previous frames.
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));

    const float size = static_cast<float>(visual.width_px);
    const float radius = static_cast<float>(visual.radius_px);
    // Half-pixel insets keep a 1px stroke on exactly one pixel row/column.
    const Gdiplus::RectF outer_rect(0.5f, 0.5f, size - 1.0f, size - 1.0f);

    // 1. Panel wash: the glassy fill that darkens the window's outer band and
    //    lets a little of what is behind it show through.
    if (visual.draw_fill && visual.fill.a > 0) {
        Gdiplus::GraphicsPath path;
        add_rounded_rect(path, outer_rect, radius);
        Gdiplus::SolidBrush brush(to_color(visual.fill));
        graphics.FillPath(&brush, &path);
    }

    // 2. Soft inner shadow: a few concentric strokes with a quadratic falloff,
    //    drawn outermost (brightest) last so the alpha accumulates into a
    //    gradient near the edge. Bucket count is fixed, so this is a constant
    //    amount of work no matter how big the window is.
    if (visual.draw_shadow && visual.shadow.a > 0) {
        const int buckets = std::max(3, visual.width_px / 3);
        const float max_alpha = static_cast<float>(visual.shadow.a);
        for (int i = buckets; i >= 1; --i) {
            const float t = static_cast<float>(i) / static_cast<float>(buckets);  // 1 at the edge
            const float inset = 0.5f + (1.0f - t) * (size - 1.0f) * 0.65f;
            const float alpha = max_alpha * t * t / static_cast<float>(buckets);
            Gdiplus::GraphicsPath path;
            const float side = size - 2.0f * inset;
            if (side <= 1.0f) continue;
            const Gdiplus::RectF band(inset, inset, side, side);
            add_rounded_rect(path, band, std::max(0.0f, radius - inset));
            Gdiplus::Pen pen(Gdiplus::Color(static_cast<BYTE>(std::min(255.0f, alpha)), 0, 0, 0), 1.6f);
            pen.SetLineJoin(Gdiplus::LineJoinRound);
            graphics.DrawPath(&pen, &path);
        }
    }

    // 3. Top highlight: one barely-visible line along the inner top edge. Reads
    //    as light falling on glass without becoming a "glow".
    if (visual.draw_highlight && visual.highlight.a > 0) {
        const float inset = size - 0.5f;
        const float left = radius;
        const float right = size - radius;
        if (right > left) {
            Gdiplus::Pen pen(to_color(visual.highlight), 1.0f);
            graphics.DrawLine(&pen, left, inset, right, inset);
        }
    }

    // 4. The hairline border itself.
    if (visual.border.a > 0) {
        Gdiplus::GraphicsPath path;
        add_rounded_rect(path, outer_rect, radius);
        Gdiplus::Pen pen(to_color(visual.border), 1.0f);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        graphics.DrawPath(&pen, &path);
    }

    if (graphics.GetLastStatus() != Gdiplus::Ok) {
        if (error) *error = "GDI+ draw call reported an error";
        return false;
    }
    graphics.Flush(Gdiplus::FlushIntentionSync);
    return true;
}

}  // namespace win
}  // namespace azy
