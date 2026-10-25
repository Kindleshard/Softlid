#include "ui/Canvas.h"
#include "core/Lang.h"
#include <algorithm>
#include <map>
#include <tuple>

namespace bb::ui {
namespace {

using gfx::ComPtr;

const wchar_t* GlyphFamily()
{
    static const wchar_t* family = [] {
        ComPtr<IDWriteFontCollection> fonts;
        gfx::Write()->GetSystemFontCollection(fonts.GetAddressOf());
        UINT32 index;
        BOOL exists = FALSE;
        if (fonts) fonts->FindFamilyName(L"Segoe Fluent Icons", &index, &exists);
        return exists ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
    }();
    return family;
}

std::map<std::tuple<int, int, bool>, ComPtr<IDWriteTextFormat>>& Cache()
{
    static std::map<std::tuple<int, int, bool>, ComPtr<IDWriteTextFormat>> cache;
    return cache;
}

IDWriteTextFormat* Cached(float points, gfx::Weight weight, bool glyphs)
{
    auto key = std::make_tuple(static_cast<int>(points * 100), static_cast<int>(weight), glyphs);
    auto& slot = Cache()[key];
    if (!slot) slot = gfx::Font(points, weight, glyphs ? GlyphFamily() : nullptr);
    return slot.Get();
}

ComPtr<IDWriteTextLayout> Layout(std::wstring_view text, IDWriteTextFormat* font, float w, float h)
{
    ComPtr<IDWriteTextLayout> layout;
    gfx::Write()->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), font, std::max(0.0f, w), std::max(0.0f, h),
                                   layout.GetAddressOf());
    return layout;
}

} // namespace

IDWriteTextFormat* Fonts::Body() { return Cached(10, gfx::Weight::Normal, false); }
IDWriteTextFormat* Fonts::Small() { return Cached(8.75f, gfx::Weight::Normal, false); }
IDWriteTextFormat* Fonts::Heading() { return Cached(11.5f, gfx::Weight::Semibold, false); }
IDWriteTextFormat* Fonts::Get(float points, gfx::Weight weight) { return Cached(points, weight, false); }
IDWriteTextFormat* Fonts::Glyphs(float points) { return Cached(points, gfx::Weight::Normal, true); }
void Fonts::Release() { Cache().clear(); }

Canvas::Canvas(ID2D1RenderTarget* target) : target(target)
{
    target->CreateSolidColorBrush(gfx::Color(0), brush.GetAddressOf());
    D2D1_STROKE_STYLE_PROPERTIES round{};
    round.startCap = round.endCap = D2D1_CAP_STYLE_ROUND;
    gfx::D2D()->CreateStrokeStyle(round, nullptr, 0, roundCaps.GetAddressOf());
}

ID2D1SolidColorBrush* Canvas::Brush(int argb, float alpha)
{
    brush->SetColor(gfx::Color(argb, alpha));
    return brush.Get();
}

void Canvas::Fill(const D2D1_RECT_F& r, int argb, float alpha) { target->FillRectangle(r, Brush(argb, alpha)); }

void Canvas::FillRound(const D2D1_RECT_F& r, float radius, int argb, float alpha)
{
    target->FillRoundedRectangle({r, radius, radius}, Brush(argb, alpha));
}

void Canvas::OutlineRound(const D2D1_RECT_F& r, float radius, int argb, float width, float alpha)
{
    target->DrawRoundedRectangle({r, radius, radius}, Brush(argb, alpha), width);
}

void Canvas::FillCircle(float cx, float cy, float radius, int argb, float alpha)
{
    target->FillEllipse({{cx, cy}, radius, radius}, Brush(argb, alpha));
}

void Canvas::OutlineCircle(float cx, float cy, float radius, int argb, float width, float alpha)
{
    target->DrawEllipse({{cx, cy}, radius, radius}, Brush(argb, alpha), width);
}

void Canvas::Line(float x1, float y1, float x2, float y2, int argb, float width, bool roundEnds)
{
    target->DrawLine({x1, y1}, {x2, y2}, Brush(argb), width, roundEnds ? roundCaps.Get() : nullptr);
}

void Canvas::Text(std::wstring_view text, IDWriteTextFormat* font, const D2D1_RECT_F& box, int argb, HAlign align, bool wrap)
{
    if (text.empty()) return;
    ComPtr<IDWriteTextLayout> layout = Layout(text, font, Width(box), Height(box));
    if (!layout) return;
    layout->SetTextAlignment(align == HAlign::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
                             : align == HAlign::Right ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING);
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    layout->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    ComPtr<IDWriteInlineObject> ellipsis;
    gfx::Write()->CreateEllipsisTrimmingSign(layout.Get(), ellipsis.GetAddressOf());
    layout->SetTrimming(&trim, ellipsis.Get());
    target->DrawTextLayout({box.left, box.top}, layout.Get(), Brush(argb), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void Canvas::Glyph(wchar_t glyph, IDWriteTextFormat* font, const D2D1_RECT_F& box, int argb)
{
    constexpr float room = 200;
    ComPtr<IDWriteTextLayout> layout = Layout({&glyph, 1}, font, room, room);
    if (!layout) return;
    layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    DWRITE_OVERHANG_METRICS ink{};
    layout->GetOverhangMetrics(&ink);
    // The ink, relative to the layout's top left: overhangs are how far it pokes out of the (room x room) box.
    float left = -ink.left, top = -ink.top, right = room + ink.right, bottom = room + ink.bottom;
    float x = (box.left + box.right) / 2 - (left + right) / 2, y = (box.top + box.bottom) / 2 - (top + bottom) / 2;
    target->DrawTextLayout({x, y}, layout.Get(), Brush(argb));
}

float TextWidth(std::wstring_view text, IDWriteTextFormat* font)
{
    ComPtr<IDWriteTextLayout> layout = Layout(text, font, 10000, 1000);
    DWRITE_TEXT_METRICS m{};
    if (layout) {
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        layout->GetMetrics(&m);
    }
    return m.widthIncludingTrailingWhitespace;
}

float LineHeight(IDWriteTextFormat* font)
{
    ComPtr<IDWriteTextLayout> layout = Layout(L"Ag", font, 1000, 1000);
    DWRITE_TEXT_METRICS m{};
    if (layout) layout->GetMetrics(&m);
    return m.height;
}

int Mix(int from, int to, float k)
{
    auto channel = [&](int shift) {
        int a = (from >> shift) & 0xFF, b = (to >> shift) & 0xFF;
        return static_cast<unsigned>(a + (b - a) * k + 0.5f) & 0xFF;
    };
    return static_cast<int>(0xFF000000u | (channel(16) << 16) | (channel(8) << 8) | channel(0));
}

} // namespace bb::ui
