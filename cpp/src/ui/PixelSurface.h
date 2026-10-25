#pragma once
#include <windows.h>
#include <cstdint>

namespace bb {

// Pixels for a layered window, written directly (premultiplied BGRA, top-down rows) and shown with
// UpdateLayeredWindow: for pictures made pixel by pixel (the eyelids, the liquid) rather than drawn with Direct2D.
class PixelSurface {
public:
    PixelSurface() = default;
    ~PixelSurface() { Free(); }
    PixelSurface(const PixelSurface&) = delete;
    PixelSurface& operator=(const PixelSurface&) = delete;

    // Makes room for at least w x h pixels (with some spare when `spare`, so a growing area doesn't reallocate every
    // frame); the contents are kept only if no new room was needed.
    void Ensure(int w, int h, bool spare = false);
    uint32_t* Row(int y) { return bits + static_cast<size_t>(y) * width; }
    int Stride() const { return width; }   // pixels from one row to the next

    // Shows the top-left w x h of it on `window`, at `at` on screen, at an overall opacity.
    void Present(HWND window, POINT at, int w, int h, BYTE opacity = 255);
    void Free();

private:
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    uint32_t* bits = nullptr;
    int width = 0, height = 0;
};

} // namespace bb
