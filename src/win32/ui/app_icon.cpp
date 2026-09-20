#include "azy/win32/ui/app_icon.hpp"

#include <vector>

#include "azy/core/theme.hpp"

namespace azy {
namespace win {
namespace {

HICON g_icon = nullptr;
bool g_icon_is_shared = false;

// Small helper: an off-screen 32-bit DIB we can draw the icon into.
struct IconCanvas {
    HDC dc = nullptr;
    HBITMAP color = nullptr;
    HBITMAP mask = nullptr;
    void* bits = nullptr;
    int width = 0;
    int height = 0;

    ~IconCanvas() { release(); }

    bool create(int size) {
        width = size;
        height = size;
        HDC screen = GetDC(nullptr);
        if (!screen) return false;

        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = size;
        info.bmiHeader.biHeight = -size;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        color = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        dc = CreateCompatibleDC(screen);
        ReleaseDC(nullptr, screen);
        if (!color || !dc) return false;
        SelectObject(dc, color);

        mask = CreateBitmap(size, size, 1, 1, nullptr);
        if (!mask) return false;
        return true;
    }

    void release() {
        if (dc) {
            DeleteDC(dc);
            dc = nullptr;
        }
        if (color) {
            DeleteObject(color);
            color = nullptr;
        }
        if (mask) {
            DeleteObject(mask);
            mask = nullptr;
        }
    }
};

void fill_alpha(void* bits, int size, unsigned char alpha) {
    auto* pixels = static_cast<unsigned int*>(bits);
    const unsigned int value = (static_cast<unsigned int>(alpha) << 24);
    for (int i = 0; i < size * size; ++i) {
        pixels[i] = value;  // black, alpha only: GDI drawing writes BGR and keeps alpha 0
    }
}

void mark_opaque(void* bits, int size) {
    auto* pixels = static_cast<unsigned int*>(bits);
    for (int i = 0; i < size * size; ++i) {
        pixels[i] |= 0xFF000000u;  // everything GDI drew becomes fully opaque
    }
}

}  // namespace

HICON app_icon() {
    if (g_icon != nullptr) return g_icon;

    // Preferred: the icon embedded in the executable's resources (resources/
    // azy_skin.rc), so the tray, Explorer, shortcuts and the taskbar all show the
    // same artwork.
    if (HICON embedded = static_cast<HICON>(LoadImageW(
            GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED))) {
        g_icon = embedded;
        g_icon_is_shared = true;
        return g_icon;
    }

    // Fallback (e.g. a build without the resource script): draw the same mark.
    // Note that a drawn icon is owned by this module rather than the shell.
    const int size = GetSystemMetrics(SM_CXSMICON);
    const int icon_size = size > 0 ? size : 16;

    IconCanvas canvas;
    if (!canvas.create(icon_size)) return LoadIconW(nullptr, IDI_APPLICATION);
    fill_alpha(canvas.bits, icon_size, 0);

    // Rounded charcoal tile.
    HBRUSH background = CreateSolidBrush(RGB(28, 28, 32));
    HBRUSH border = CreateSolidBrush(RGB(70, 72, 80));
    HGDIOBJ old_brush = SelectObject(canvas.dc, background);
    HGDIOBJ old_pen = SelectObject(canvas.dc, GetStockObject(NULL_PEN));
    const int inset = icon_size >= 24 ? 2 : 1;
    const int radius = icon_size >= 24 ? 7 : 4;
    RoundRect(canvas.dc, inset, inset, icon_size - inset, icon_size - inset, radius, radius);
    SelectObject(canvas.dc, border);
    SelectObject(canvas.dc, CreatePen(PS_SOLID, 1, RGB(74, 76, 84)));
    SelectObject(canvas.dc, GetStockObject(NULL_BRUSH));
    RoundRect(canvas.dc, inset, inset, icon_size - inset, icon_size - inset, radius, radius);
    SelectObject(canvas.dc, old_brush);
    SelectObject(canvas.dc, old_pen);
    DeleteObject(background);
    DeleteObject(border);

    // The "A".
    HFONT font = CreateFontW(-icon_size + (icon_size >= 24 ? 8 : 5), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    HGDIOBJ old_font = SelectObject(canvas.dc, font);
    SetBkMode(canvas.dc, TRANSPARENT);
    SetTextColor(canvas.dc, RGB(226, 228, 234));
    RECT text_rect{0, 0, icon_size, icon_size};
    DrawTextW(canvas.dc, L"A", 1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(canvas.dc, old_font);
    DeleteObject(font);

    mark_opaque(canvas.bits, icon_size);

    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmColor = canvas.color;
    info.hbmMask = canvas.mask;
    g_icon = CreateIconIndirect(&info);
    if (g_icon == nullptr) return LoadIconW(nullptr, IDI_APPLICATION);
    return g_icon;
}

void destroy_app_icon() {
    if (g_icon != nullptr) {
        // Icons loaded with LR_SHARED must not be destroyed; ones drawn here must.
        // The shell reuses shared icons, so only destroy what we created. We track
        // that with a flag rather than guessing.
        if (!g_icon_is_shared) DestroyIcon(g_icon);
        g_icon = nullptr;
        g_icon_is_shared = false;
    }
}

}  // namespace win
}  // namespace azy
