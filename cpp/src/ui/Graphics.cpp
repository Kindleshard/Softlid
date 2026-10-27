#include "ui/Graphics.h"
#include "core/Lang.h"
#include <cmath>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace bb::gfx {

ID2D1Factory* D2D()
{
    static ComPtr<ID2D1Factory> factory;
    if (!factory) D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf());
    return factory.Get();
}

IDWriteFactory* Write()
{
    static ComPtr<IDWriteFactory> factory;
    if (!factory) DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(factory.GetAddressOf()));
    return factory.Get();
}

D2D1_COLOR_F Color(int argb, float alpha)
{
    auto u = static_cast<unsigned>(argb);
    return {((u >> 16) & 0xFF) / 255.0f, ((u >> 8) & 0xFF) / 255.0f, (u & 0xFF) / 255.0f, ((u >> 24) & 0xFF) / 255.0f * alpha};
}

ComPtr<IDWriteTextFormat> Font(float points, Weight weight, const wchar_t* family)
{
    DWRITE_FONT_WEIGHT w = weight == Weight::Light ? DWRITE_FONT_WEIGHT_LIGHT
                         : weight == Weight::Semibold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
    ComPtr<IDWriteTextFormat> format;
    Write()->CreateTextFormat(family ? family : Lang::Family(), nullptr, w, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                              points * 96.0f / 72.0f, L"", format.GetAddressOf());
    return format;
}

void Text(ID2D1RenderTarget* target, const std::wstring& text, IDWriteTextFormat* font, const D2D1_RECT_F& box,
          int argb, Align align, bool topAligned, bool wrap)
{
    font->SetTextAlignment(align == Align::Center ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);
    font->SetParagraphAlignment(topAligned ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR : DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    font->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    ComPtr<ID2D1SolidColorBrush> brush;
    target->CreateSolidColorBrush(Color(argb), brush.GetAddressOf());
    target->DrawText(text.c_str(), static_cast<UINT32>(text.size()), font, box, brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void Fill(ID2D1RenderTarget* target, const D2D1_ROUNDED_RECT& shape, int argb, float alpha)
{
    ComPtr<ID2D1SolidColorBrush> brush;
    target->CreateSolidColorBrush(Color(argb, alpha), brush.GetAddressOf());
    target->FillRoundedRectangle(shape, brush.Get());
}

void Fill(ID2D1RenderTarget* target, const D2D1_ELLIPSE& shape, int argb, float alpha)
{
    ComPtr<ID2D1SolidColorBrush> brush;
    target->CreateSolidColorBrush(Color(argb, alpha), brush.GetAddressOf());
    target->FillEllipse(shape, brush.Get());
}

void Outline(ID2D1RenderTarget* target, const D2D1_ROUNDED_RECT& shape, int argb, float width)
{
    ComPtr<ID2D1SolidColorBrush> brush;
    target->CreateSolidColorBrush(Color(argb), brush.GetAddressOf());
    target->DrawRoundedRectangle(shape, brush.Get(), width);
}

ComPtr<ID2D1StrokeStyle> RoundCaps()
{
    ComPtr<ID2D1StrokeStyle> s;
    D2D1_STROKE_STYLE_PROPERTIES p{};
    p.startCap = p.endCap = D2D1_CAP_STYLE_ROUND;
    D2D()->CreateStrokeStyle(p, nullptr, 0, s.GetAddressOf());
    return s;
}

void Arc(ID2D1RenderTarget* target, float cx, float cy, float r, float start, float sweep, ID2D1Brush* brush, float width,
         ID2D1StrokeStyle* style)
{
    if (sweep < 0.5f) return;
    if (sweep >= 359.9f) {
        target->DrawEllipse({{cx, cy}, r, r}, brush, width);
        return;
    }
    constexpr float Rad = 3.14159265f / 180;
    auto at = [&](float deg) { return D2D1_POINT_2F{cx + r * std::cos(deg * Rad), cy + r * std::sin(deg * Rad)}; };
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    D2D()->CreatePathGeometry(path.GetAddressOf());
    path->Open(sink.GetAddressOf());
    sink->BeginFigure(at(start), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc({at(start + sweep), {r, r}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, sweep > 180 ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL});
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    target->DrawGeometry(path.Get(), brush, width, style);
}

} // namespace bb::gfx
