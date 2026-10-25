#pragma once
#include "ui/Graphics.h"
#include <string>

namespace bb::ui {

// The fonts of the interface, made once and shared (sizes in points, as in the C# version).
namespace Fonts {
IDWriteTextFormat* Body();      // Segoe UI 10 (or the CJK family for those languages)
IDWriteTextFormat* Small();     // 8.75
IDWriteTextFormat* Heading();   // semibold 11.5
IDWriteTextFormat* Get(float points, gfx::Weight weight = gfx::Weight::Normal);
IDWriteTextFormat* Glyphs(float points = 10);   // Segoe Fluent Icons (Windows 11) or Segoe MDL2 Assets (Windows 10)
void Release();                 // when the last window closes (they come back on next use)
} // namespace Fonts

enum class HAlign { Left, Center, Right };

// Drawing for controls, on a render target, with one brush that changes colour rather than a brush per shape.
// Coordinates are in 96-dpi units (the window scales them for the display).
class Canvas {
public:
    explicit Canvas(ID2D1RenderTarget* target);
    ID2D1RenderTarget* Target() const { return target; }
    ID2D1SolidColorBrush* Brush(int argb, float alpha = 1);

    void Fill(const D2D1_RECT_F& r, int argb, float alpha = 1);
    void FillRound(const D2D1_RECT_F& r, float radius, int argb, float alpha = 1);
    void OutlineRound(const D2D1_RECT_F& r, float radius, int argb, float width = 1, float alpha = 1);
    void FillCircle(float cx, float cy, float radius, int argb, float alpha = 1);
    void OutlineCircle(float cx, float cy, float radius, int argb, float width = 1, float alpha = 1);
    void Line(float x1, float y1, float x2, float y2, int argb, float width = 1, bool roundEnds = true);

    // One line of text in a box, centred vertically; cut short with "..." when it doesn't fit (or wrapped).
    void Text(std::wstring_view text, IDWriteTextFormat* font, const D2D1_RECT_F& box, int argb,
              HAlign align = HAlign::Left, bool wrap = false, bool top = false);   // top: from the top, not centred
    // A single icon character, centred in a box by its own outline (font metrics would leave it a pixel or so off).
    void Glyph(wchar_t glyph, IDWriteTextFormat* font, const D2D1_RECT_F& box, int argb);

private:
    ID2D1RenderTarget* target;
    gfx::ComPtr<ID2D1SolidColorBrush> brush;
    gfx::ComPtr<ID2D1StrokeStyle> roundCaps;
};

// Text sizes, without drawing.
float TextWidth(std::wstring_view text, IDWriteTextFormat* font);
float LineHeight(IDWriteTextFormat* font);
float TextHeight(std::wstring_view text, IDWriteTextFormat* font, float width);   // wrapped at a width

// Rectangle helpers.
inline D2D1_RECT_F Rect(float x, float y, float w, float h) { return {x, y, x + w, y + h}; }
inline D2D1_RECT_F Inset(D2D1_RECT_F r, float dx, float dy) { return {r.left + dx, r.top + dy, r.right - dx, r.bottom - dy}; }
inline float Width(const D2D1_RECT_F& r) { return r.right - r.left; }
inline float Height(const D2D1_RECT_F& r) { return r.bottom - r.top; }
inline bool Contains(const D2D1_RECT_F& r, D2D1_POINT_2F p) { return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom; }

int Mix(int from, int to, float k);   // between two colours (0 from .. 1 to)

} // namespace bb::ui
