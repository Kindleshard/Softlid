#pragma once
#include "ui/Graphics.h"
#include <windows.h>

namespace bb {

// A picture with per-pixel transparency for a layered window: smooth round edges on any background. Draw into it
// with Direct2D (in 96-dpi units: the scale is applied for you), then Present it at a place on screen.
class LayeredCanvas {
public:
    LayeredCanvas() = default;
    ~LayeredCanvas();
    LayeredCanvas(const LayeredCanvas&) = delete;
    LayeredCanvas& operator=(const LayeredCanvas&) = delete;

    // Starts a frame of width x height pixels at the given scale, cleared to transparent. Returns null on failure.
    ID2D1RenderTarget* Begin(int width, int height, float scale);
    void End();
    void Present(HWND window, POINT at, BYTE opacity = 255);   // opacity also fades the whole picture
    void Release();   // gives the memory back (it is made again on the next Begin)

    int Width() const { return width; }
    int Height() const { return height; }

private:
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    int width = 0, height = 0;
    gfx::ComPtr<ID2D1DCRenderTarget> target;
};

} // namespace bb
