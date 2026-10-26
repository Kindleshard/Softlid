#pragma once
#include "ui/Graphics.h"
#include <d2d1_1.h>
#include <windows.h>

namespace bb::ui {

// Direct2D drawing on the processor into a GDI device context - what a DC render target does, but on one software
// device shared by every window and made without worker threads of its own. (Each DC render target brings up its own
// software device with about eight threads, and a few MB more on a real monitor; for a settings page and a small icon,
// one thread draws them just as well.) The device goes when the last canvas is released.
class SoftCanvas {
public:
    SoftCanvas() = default;
    ~SoftCanvas();
    SoftCanvas(const SoftCanvas&) = delete;
    SoftCanvas& operator=(const SoftCanvas&) = delete;

    // Starts drawing an area of a device context: (0, 0) is the area's corner, in 96-dpi units at `dpi`. alpha: keep
    // transparency (premultiplied, for layered windows; text then grayscale); otherwise opaque, with ClearType text.
    // Null if Direct2D can't draw here.
    ID2D1RenderTarget* Begin(HDC dc, const RECT& area, float dpi, bool alpha = false);
    // Puts what was drawn into the device context; false when the device was lost (the next Begin makes a new one).
    bool End();
    void Release();   // gives the memory back (made again on the next Begin)

private:
    gfx::ComPtr<ID2D1DeviceContext> context;
    gfx::ComPtr<ID2D1GdiInteropRenderTarget> gdi;
    gfx::ComPtr<ID2D1Bitmap1> bitmap;   // what is drawn into: at least as big as the largest area so far
    HDC dc = nullptr;
    RECT area{};
    bool alpha = false;
};

} // namespace bb::ui
