#include "features/mini/MiniMorph.h"
#include "core/Palette.h"
#include <dwmapi.h>
#include <algorithm>
#include <cmath>

namespace bb {
namespace {

constexpr UINT_PTR FrameTimer = 1;
constexpr double Duration = 280;                  // ms
constexpr int Disc = Palette::Argb(34, 36, 42);   // mini mode's dark disc

double Ease(double t) { return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2; }   // slow, quick, slow
double Lerp(double a, double b, double k) { return a + (b - a) * k; }

} // namespace

MiniMorph::Picture MiniMorph::Take(HWND window)
{
    Picture p;
    RECT whole{}, frame{};
    GetWindowRect(window, &whole);
    // Only what is seen: Windows 10 and 11 frames have invisible resize borders around them.
    if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof frame))) frame = whole;
    int w = whole.right - whole.left, h = whole.bottom - whole.top;
    if (w <= 0 || h <= 0) return p;
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib) {
        DeleteDC(dc);
        return p;
    }
    HGDIOBJ old = SelectObject(dc, dib);
    PrintWindow(window, dc, PW_RENDERFULLCONTENT);
    GdiFlush();
    int x0 = std::clamp<int>(frame.left - whole.left, 0, w), y0 = std::clamp<int>(frame.top - whole.top, 0, h);
    int x1 = std::clamp<int>(frame.right - whole.left, x0, w), y1 = std::clamp<int>(frame.bottom - whole.top, y0, h);
    p.width = x1 - x0;
    p.height = y1 - y0;
    p.at = {whole.left + x0, whole.top + y0, whole.left + x1, whole.top + y1};
    p.pixels.resize(static_cast<size_t>(p.width) * p.height);
    auto src = static_cast<const uint32_t*>(bits);
    for (int y = 0; y < p.height; y++)
        for (int x = 0; x < p.width; x++) p.pixels[static_cast<size_t>(y) * p.width + x] = src[(y0 + y) * w + x0 + x] | 0xFF000000u;
    SelectObject(dc, old);
    DeleteObject(dib);
    DeleteDC(dc);
    return p;
}

MiniMorph::MiniMorph(Picture shot, RECT target, std::function<void()> done)
    : picture(std::move(shot)), to(target), landed(std::move(done))
{
    Create(L"Softlid.Morph", WS_POPUP, PopupExStyle | WS_EX_LAYERED | WS_EX_TRANSPARENT, picture.at);
    start = GetTickCount64();
    Frame();
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    SetTimer(hwnd, FrameTimer, 15, nullptr);
}

MiniMorph::~MiniMorph() { Destroy(); }

LRESULT MiniMorph::OnMessage(UINT message, WPARAM w, LPARAM l)
{
    if (message == WM_TIMER && w == FrameTimer) {
        Frame();
        return 0;
    }
    return Window::OnMessage(message, w, l);
}

// One frame: the picture in a rounded rectangle between the window's place and the icon's, its corners rounding into
// a circle and its colours giving way to the disc in the last part of the way.
void MiniMorph::Frame()
{
    double t = std::min(1.0, (GetTickCount64() - start) / Duration), k = Ease(t);
    const RECT& from = picture.at;
    double cx = Lerp((from.left + from.right) / 2.0, (to.left + to.right) / 2.0, k);
    double cy = Lerp((from.top + from.bottom) / 2.0, (to.top + to.bottom) / 2.0, k);
    int w = std::max(1, static_cast<int>(std::lround(Lerp(from.right - from.left, to.right - to.left, k))));
    int h = std::max(1, static_cast<int>(std::lround(Lerp(from.bottom - from.top, to.bottom - to.top, k))));
    POINT at{static_cast<LONG>(std::lround(cx - w / 2.0)), static_cast<LONG>(std::lround(cy - h / 2.0))};

    if (ID2D1RenderTarget* target = canvas.Begin(w, h, 1.0f)) {
        if (!bitmap || madeFor != target) {
            auto props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            bitmap.Reset();
            target->CreateBitmap(D2D1::SizeU(picture.width, picture.height), picture.pixels.data(), picture.width * 4, props,
                                 bitmap.GetAddressOf());
            madeFor = target;
        }
        float fw = static_cast<float>(w), fh = static_cast<float>(h);
        float radius = static_cast<float>(Lerp(8, std::min(fw, fh) / 2, std::pow(k, 0.8)));
        D2D1_ROUNDED_RECT shape{{0, 0, fw, fh}, radius, radius};
        if (bitmap) {
            gfx::ComPtr<ID2D1BitmapBrush> brush;
            target->CreateBitmapBrush(bitmap.Get(), brush.GetAddressOf());
            if (brush) {
                brush->SetTransform(D2D1::Matrix3x2F::Scale(fw / picture.width, fh / picture.height));
                brush->SetInterpolationMode(D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                target->FillRoundedRectangle(shape, brush.Get());
            }
        }
        double disc = std::clamp((t - 0.45) / 0.55, 0.0, 1.0);   // the icon's dark disc comes through at the end
        if (disc > 0) {
            gfx::ComPtr<ID2D1SolidColorBrush> brush;
            target->CreateSolidColorBrush(gfx::Color(Disc, static_cast<float>(disc * disc)), brush.GetAddressOf());
            if (brush) target->FillRoundedRectangle(shape, brush.Get());
        }
        canvas.End();
        canvas.Present(hwnd, at, 255);
    }
    if (t >= 1) {
        KillTimer(hwnd, FrameTimer);
        ShowWindow(hwnd, SW_HIDE);
        if (landed) {
            auto done = std::move(landed);
            landed = nullptr;
            done();   // (the owner may delete this window now)
        }
    }
}

} // namespace bb
