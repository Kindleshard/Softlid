#include "ui/PixelSurface.h"
#include <algorithm>

namespace bb {

void PixelSurface::Ensure(int w, int h, bool spare)
{
    if (w <= width && h <= height && dc) return;
    int nw = spare ? std::max(w, width) + 64 : w, nh = spare ? std::max(h, height) + 64 : h;
    Free();
    width = std::max(1, nw);
    height = std::max(1, nh);
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};   // top-down rows
    void* p = nullptr;
    HDC screen = GetDC(nullptr);
    dc = CreateCompatibleDC(screen);
    bitmap = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &p, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!dc || !bitmap) {
        Free();
        return;
    }
    bits = static_cast<uint32_t*>(p);
    previous = SelectObject(dc, bitmap);
}

void PixelSurface::Present(HWND window, POINT at, int w, int h, BYTE opacity)
{
    if (!dc || w <= 0 || h <= 0) return;
    SIZE size{std::min(w, width), std::min(h, height)};
    POINT from{};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, opacity, AC_SRC_ALPHA};
    UpdateLayeredWindow(window, nullptr, &at, &size, dc, &from, 0, &blend, ULW_ALPHA);
}

void PixelSurface::Free()
{
    if (dc) {
        SelectObject(dc, previous);
        DeleteDC(dc);
    }
    if (bitmap) DeleteObject(bitmap);
    dc = nullptr;
    bitmap = nullptr;
    bits = nullptr;
    width = height = 0;
}

} // namespace bb
