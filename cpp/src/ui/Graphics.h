#pragma once
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>

namespace bb::gfx {

using Microsoft::WRL::ComPtr;

// Direct2D and DirectWrite, made on first use and shared by every window.
ID2D1Factory* D2D();
IDWriteFactory* Write();

D2D1_COLOR_F Color(int argb, float alpha = 1.0f);   // an ARGB colour as Direct2D wants it (alpha multiplies)

// Text. Sizes are in points, as in the C# version (Segoe UI 10 is the body text); drawing is in 96-dpi units.
enum class Weight { Light, Normal, Semibold };
ComPtr<IDWriteTextFormat> Font(float points, Weight weight = Weight::Normal, const wchar_t* family = nullptr);

enum class Align { Left, Center };
void Text(ID2D1RenderTarget* target, const std::wstring& text, IDWriteTextFormat* font, const D2D1_RECT_F& box,
          int argb, Align align = Align::Left, bool topAligned = false, bool wrap = false);

// Filled and outlined shapes with a brush made for the call (fine for the small, rarely drawn pop-ups).
void Fill(ID2D1RenderTarget* target, const D2D1_ROUNDED_RECT& shape, int argb, float alpha = 1.0f);
void Fill(ID2D1RenderTarget* target, const D2D1_ELLIPSE& shape, int argb, float alpha = 1.0f);
void Outline(ID2D1RenderTarget* target, const D2D1_ROUNDED_RECT& shape, int argb, float width = 1.0f);

// A pill: a rectangle with fully round ends.
inline D2D1_ROUNDED_RECT Pill(float x, float y, float w, float h) { return {{x, y, x + w, y + h}, h / 2, h / 2}; }

} // namespace bb::gfx
