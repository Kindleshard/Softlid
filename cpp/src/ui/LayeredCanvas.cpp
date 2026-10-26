#include "ui/LayeredCanvas.h"

namespace bb {

LayeredCanvas::~LayeredCanvas() { Release(); }

void LayeredCanvas::Release()
{
    canvas.Release();
    if (dc) {
        SelectObject(dc, previous);
        DeleteDC(dc);
        dc = nullptr;
    }
    if (bitmap) {
        DeleteObject(bitmap);
        bitmap = nullptr;
    }
    width = height = 0;
}

ID2D1RenderTarget* LayeredCanvas::Begin(int w, int h, float scale)
{
    if (w <= 0 || h <= 0) return nullptr;
    if (w != width || h != height) {
        Release();
        // A top-down 32-bit bitmap with premultiplied alpha, which is what layered windows take.
        BITMAPINFO bi{};
        bi.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
        void* bits = nullptr;
        dc = CreateCompatibleDC(nullptr);
        bitmap = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!dc || !bitmap) { Release(); return nullptr; }
        previous = SelectObject(dc, bitmap);
        width = w;
        height = h;
    }
    // Software drawing (no graphics card device, which keeps memory down for these small pictures), with transparency
    // kept; the text is grayscale (ClearType needs an opaque background).
    RECT r{0, 0, width, height};
    ID2D1RenderTarget* target = canvas.Begin(dc, r, 96.0f * scale, true);
    if (target) target->Clear(D2D1::ColorF(0, 0, 0, 0));
    return target;
}

void LayeredCanvas::End()
{
    canvas.End();
}

void LayeredCanvas::Present(HWND window, POINT at, BYTE opacity)
{
    if (!dc) return;
    SIZE size{width, height};
    POINT origin{0, 0};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, opacity, AC_SRC_ALPHA};
    UpdateLayeredWindow(window, nullptr, &at, &size, dc, &origin, 0, &blend, ULW_ALPHA);
}

} // namespace bb
