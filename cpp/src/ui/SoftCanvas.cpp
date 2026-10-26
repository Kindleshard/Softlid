#include "ui/SoftCanvas.h"
#include <d3d11.h>
#include <algorithm>

#pragma comment(lib, "d3d11.lib")

namespace bb::ui {
namespace {

gfx::ComPtr<ID2D1Device> device;   // shared by every canvas while one exists
int canvases = 0;

// The software rasterizer (WARP), on this thread only: no internal threading, no thread safety needed.
ID2D1Device* Device()
{
    if (device) return device.Get();
    gfx::ComPtr<ID2D1Factory1> factory;
    if (FAILED(gfx::D2D()->QueryInterface(factory.GetAddressOf()))) return nullptr;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_SINGLETHREADED |
                 D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS;
    gfx::ComPtr<ID3D11Device> d3d;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION,
                                 d3d.GetAddressOf(), nullptr, nullptr)))
        return nullptr;
    gfx::ComPtr<IDXGIDevice> dxgi;
    if (FAILED(d3d.As(&dxgi)) || FAILED(factory->CreateDevice(dxgi.Get(), device.GetAddressOf()))) return nullptr;
    return device.Get();
}

} // namespace

SoftCanvas::~SoftCanvas() { Release(); }

void SoftCanvas::Release()
{
    if (!context) return;
    gdi.Reset();
    bitmap.Reset();
    context.Reset();
    if (--canvases == 0) device.Reset();
}

ID2D1RenderTarget* SoftCanvas::Begin(HDC target, const RECT& r, float dpi, bool keepAlpha)
{
    UINT w = static_cast<UINT>(std::max(0L, r.right - r.left)), h = static_cast<UINT>(std::max(0L, r.bottom - r.top));
    if (!target || w == 0 || h == 0) return nullptr;
    if (context && keepAlpha != alpha) Release();   // (the pixel format is the bitmap's)
    if (!context) {
        ID2D1Device* d = Device();
        if (!d || FAILED(d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, context.GetAddressOf()))) {
            context.Reset();
            if (canvases == 0) device.Reset();
            return nullptr;
        }
        canvases++;
        context.As(&gdi);
        alpha = keepAlpha;
    }
    D2D1_SIZE_U have = bitmap ? bitmap->GetPixelSize() : D2D1_SIZE_U{0, 0};
    if (have.width < w || have.height < h) {
        bitmap.Reset();
        context->SetTarget(nullptr);
        auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE,
                                             D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                               alpha ? D2D1_ALPHA_MODE_PREMULTIPLIED : D2D1_ALPHA_MODE_IGNORE));
        if (FAILED(context->CreateBitmap(D2D1::SizeU(std::max(w, have.width), std::max(h, have.height)), nullptr, 0, props,
                                         bitmap.GetAddressOf())))
            return nullptr;
    }
    context->SetTarget(bitmap.Get());
    context->SetDpi(dpi, dpi);   // (after SetTarget, which takes the bitmap's)
    context->SetTextAntialiasMode(alpha ? D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE : D2D1_TEXT_ANTIALIAS_MODE_DEFAULT);
    context->SetTransform(D2D1::Matrix3x2F::Identity());
    dc = target;
    area = r;
    context->BeginDraw();
    return context.Get();
}

bool SoftCanvas::End()
{
    if (!context || !dc) return true;
    HDC drawn = nullptr;
    if (gdi && SUCCEEDED(gdi->GetDC(D2D1_DC_INITIALIZE_MODE_COPY, &drawn))) {
        BitBlt(dc, area.left, area.top, area.right - area.left, area.bottom - area.top, drawn, 0, 0, SRCCOPY);
        gdi->ReleaseDC(nullptr);
    }
    dc = nullptr;
    HRESULT hr = context->EndDraw();
    context->SetTarget(nullptr);
    if (hr == D2DERR_RECREATE_TARGET) {
        Release();
        device.Reset();   // (lost: every canvas makes a new one; resources made on the old one are dropped by their owners)
        return false;
    }
    return true;
}

} // namespace bb::ui
