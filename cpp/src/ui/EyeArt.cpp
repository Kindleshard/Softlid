#include "ui/EyeArt.h"
#include "ui/Graphics.h"
#include "ui/Theme.h"
#include <algorithm>

namespace bb::EyeArt {
namespace {

D2D1_POINT_2F Bezier(D2D1_POINT_2F a, D2D1_POINT_2F b, D2D1_POINT_2F c, D2D1_POINT_2F d, float t)
{
    float u = 1 - t, w0 = u * u * u, w1 = 3 * u * u * t, w2 = 3 * u * t * t, w3 = t * t * t;
    return {w0 * a.x + w1 * b.x + w2 * c.x + w3 * d.x, w0 * a.y + w1 * b.y + w2 * c.y + w3 * d.y};
}

gfx::ComPtr<ID2D1SolidColorBrush> Brush(ID2D1RenderTarget* target, int argb, float alpha)
{
    gfx::ComPtr<ID2D1SolidColorBrush> brush;
    target->CreateSolidColorBrush(gfx::Color(argb, alpha), brush.GetAddressOf());
    return brush;
}

} // namespace

void Draw(ID2D1RenderTarget* target, const D2D1_RECT_F& box, float open, float lookX, float lookY, float stroke, float alpha)
{
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    float hw = (box.right - box.left) / 2, hh = (box.bottom - box.top) / 2;
    float cx = box.left + hw, cy = box.top + hh + hh * 0.15f;
    // A cubic Bezier bulges 3/4 of the way to its control points; the lids span -hh..+0.7hh around cy.
    float lowY = cy + hh * 0.93f;
    float upY = lowY + (cy - hh * 1.33f - lowY) * open;
    D2D1_POINT_2F left{box.left, cy}, right{box.right, cy};
    D2D1_POINT_2F u1{box.left + hw * 0.3f, upY}, u2{box.right - hw * 0.3f, upY};
    D2D1_POINT_2F l1{box.left + hw * 0.3f, lowY}, l2{box.right - hw * 0.3f, lowY};

    gfx::ComPtr<ID2D1PathGeometry> lid;
    gfx::D2D()->CreatePathGeometry(lid.GetAddressOf());
    gfx::ComPtr<ID2D1GeometrySink> sink;
    lid->Open(sink.GetAddressOf());
    sink->BeginFigure(left, D2D1_FIGURE_BEGIN_FILLED);
    sink->AddBezier({u1, u2, right});
    sink->AddBezier({l2, l1, left});
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();

    gfx::ComPtr<ID2D1StrokeStyle> round;
    gfx::D2D()->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                              D2D1_LINE_JOIN_ROUND), nullptr, 0, round.GetAddressOf());

    // A soft dark halo so the eye reads on light backgrounds too.
    target->DrawGeometry(lid.Get(), Brush(target, Palette::Argb(0, 0, 0), alpha * 90 / 255.0f).Get(), stroke * 2.4f, round.Get());
    if (open > 0.03f) {
        target->FillGeometry(lid.Get(), Brush(target, Palette::Argb(246, 247, 250), alpha).Get());
        // The iris, pupil and highlight, cut to the lids.
        target->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), lid.Get()), nullptr);
        float ir = hh * 0.8f, ix = cx + lookX * hw * 0.38f, iy = cy - hh * 0.12f + lookY * hh * 0.3f;
        target->FillEllipse({{ix, iy}, ir, ir}, Brush(target, Theme::Accent(), alpha).Get());
        float pr = ir * 0.5f;
        target->FillEllipse({{ix, iy}, pr, pr}, Brush(target, Palette::Argb(18, 22, 30), alpha).Get());
        float hr = ir * 0.2f;
        target->FillEllipse({{ix - ir * 0.38f, iy - ir * 0.38f}, hr, hr}, Brush(target, Palette::Argb(255, 255, 255), alpha).Get());
        target->PopLayer();
    }
    target->DrawGeometry(lid.Get(), Brush(target, Palette::Argb(255, 255, 255), alpha).Get(), stroke, round.Get());

    // Lashes, as it closes.
    if (open < 0.3f) {
        auto lash = Brush(target, Palette::Argb(255, 255, 255), alpha * (0.3f - open) / 0.3f);
        for (float t : {0.28f, 0.5f, 0.72f}) {
            D2D1_POINT_2F p = Bezier(left, l1, l2, right, t);
            target->DrawLine({p.x, p.y + stroke}, {p.x + (p.x - cx) * 0.35f, p.y + hh * 0.45f}, lash.Get(), stroke, round.Get());
        }
    }
}

void DrawPopup(ID2D1RenderTarget* target, float w, float h, float open)
{
    gfx::Fill(target, gfx::Pill(1, 1, w - 2, h - 2), Palette::Argb(24, 26, 32), 205 / 255.0f);
    float ew = w * 0.62f, eh = ew * 0.58f;
    Draw(target, {(w - ew) / 2, (h - eh) / 2, (w + ew) / 2, (h + eh) / 2}, open, 0, 0, std::max(1.5f, w * 0.026f));
}

} // namespace bb::EyeArt
