#include "ui/MiniArt.h"
#include "core/Palette.h"
#include "ui/EyeArt.h"
#include "ui/Graphics.h"
#include "ui/Theme.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace bb::MiniArt {
namespace {

using gfx::ComPtr;
constexpr float Pi = std::numbers::pi_v<float>;

constexpr int LightColor = Palette::Argb(255, 222, 140), SunCore = Palette::Argb(255, 205, 80), SunEdge = Palette::Argb(255, 150, 40);
constexpr int White = Palette::Argb(255, 255, 255);

int Mix(int a, int b, float k)
{
    k = std::clamp(k, 0.0f, 1.0f);
    auto ch = [&](int s) { return static_cast<int>(((a >> s) & 0xFF) + (((b >> s) & 0xFF) - ((a >> s) & 0xFF)) * k); };
    return Palette::Argb(ch(16), ch(8), ch(0));
}

ComPtr<ID2D1SolidColorBrush> Brush(ID2D1RenderTarget* t, int argb, float alpha = 1)
{
    ComPtr<ID2D1SolidColorBrush> b;
    t->CreateSolidColorBrush(gfx::Color(argb, alpha), b.GetAddressOf());
    return b;
}

ComPtr<ID2D1StrokeStyle> RoundStroke()
{
    ComPtr<ID2D1StrokeStyle> s;
    D2D1_STROKE_STYLE_PROPERTIES p{};
    p.startCap = p.endCap = D2D1_CAP_STYLE_ROUND;
    p.lineJoin = D2D1_LINE_JOIN_ROUND;
    gfx::D2D()->CreateStrokeStyle(p, nullptr, 0, s.GetAddressOf());
    return s;
}

// A radial gradient from `inner` (at its alpha) in the middle to `outer` at the rim.
ComPtr<ID2D1RadialGradientBrush> Radial(ID2D1RenderTarget* t, float cx, float cy, float r, D2D1_COLOR_F inner, D2D1_COLOR_F outer)
{
    D2D1_GRADIENT_STOP stops[] = {{0, inner}, {1, outer}};
    ComPtr<ID2D1GradientStopCollection> collection;
    ComPtr<ID2D1RadialGradientBrush> brush;
    t->CreateGradientStopCollection(stops, 2, collection.GetAddressOf());
    t->CreateRadialGradientBrush({{cx, cy}, {0, 0}, r, r}, collection.Get(), brush.GetAddressOf());
    return brush;
}

void Glow(ID2D1RenderTarget* t, float cx, float cy, float r, int color, float strength)
{
    if (strength <= 0.01f || r < 1) return;
    auto b = Radial(t, cx, cy, r, gfx::Color(color, std::min(1.0f, strength)), gfx::Color(color, 0));
    t->FillEllipse({{cx, cy}, r, r}, b.Get());
}

void Ray(ID2D1RenderTarget* t, float cx, float cy, double angle, float r0, float r1, ID2D1Brush* b, float width, ID2D1StrokeStyle* s)
{
    float c = static_cast<float>(std::cos(angle)), si = static_cast<float>(std::sin(angle));
    t->DrawLine({cx + c * r0, cy + si * r0}, {cx + c * r1, cy + si * r1}, b, width, s);
}

// A light bulb lit as bright as the screen: full light at full brightness, just its outline when dimmed right down.
// Hovering makes it glow a little more and sends out short rays.
void DrawLamp(ID2D1RenderTarget* t, const D2D1_RECT_F& box, float hover, float bright, int line, float stroke)
{
    float s = box.right - box.left, cx = box.left + s / 2, by = box.top + s * 0.4f, r = s * 0.26f;
    float lit = std::clamp(bright, 0.0f, 1.0f);
    Glow(t, cx, by, r * (2.1f + 0.4f * hover), LightColor, lit * (0.42f + 0.3f * hover));
    // Round top narrowing into the neck: an arc from 140 degrees, over the top, round to 40, then the neck.
    auto at = [&](float degrees) { return D2D1_POINT_2F{cx + r * std::cos(degrees * Pi / 180), by + r * std::sin(degrees * Pi / 180)}; };
    ComPtr<ID2D1PathGeometry> bulb;
    ComPtr<ID2D1GeometrySink> sink;
    gfx::D2D()->CreatePathGeometry(bulb.GetAddressOf());
    bulb->Open(sink.GetAddressOf());
    sink->BeginFigure(at(140), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddArc({at(40), {r, r}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_LARGE});
    sink->AddLine({cx + r * 0.42f, by + r * 1.18f});
    sink->AddLine({cx - r * 0.42f, by + r * 1.18f});
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    t->FillGeometry(bulb.Get(), Brush(t, Mix(LightColor, White, 0.25f * hover), 240 / 255.0f * lit).Get());
    auto round = RoundStroke();
    t->DrawGeometry(bulb.Get(), Brush(t, Mix(line, Palette::Argb(255, 214, 120), lit * 0.8f)).Get(), stroke, round.Get());
    auto ink = Brush(t, line);
    float y = by + r * 1.18f + stroke * 1.3f;
    t->DrawLine({cx - r * 0.36f, y}, {cx + r * 0.36f, y}, ink.Get(), stroke, round.Get());
    t->DrawLine({cx - r * 0.24f, y + stroke * 1.6f}, {cx + r * 0.24f, y + stroke * 1.6f}, ink.Get(), stroke, round.Get());
    if (hover <= 0.02f || lit <= 0.02f) return;
    auto rays = Brush(t, LightColor, 220 / 255.0f * hover * lit);
    for (int i = 0; i < 5; i++)   // from the left, over the top, to the right
        Ray(t, cx, by, Pi * (1.0 + 0.25 * i), r * 1.35f, r * (1.35f + 0.45f * hover), rays.Get(), stroke * 0.9f, round.Get());
}

// The sun: an outline at rest; hovering fills it with warm light, lengthens and turns its rays and makes it glow.
void DrawSun(ID2D1RenderTarget* t, float cx, float cy, float s, float hover, int line, float stroke)
{
    float r = s * 0.19f;
    Glow(t, cx, cy, r * 3.1f, SunEdge, 0.5f * hover);
    if (hover > 0.01f) {
        auto fill = Radial(t, cx, cy, r, gfx::Color(Mix(SunCore, White, 0.3f), hover), gfx::Color(SunEdge, hover));
        t->FillEllipse({{cx, cy}, r, r}, fill.Get());
    }
    int ink = Mix(line, SunCore, hover);
    auto b = Brush(t, ink);
    t->DrawEllipse({{cx, cy}, r, r}, b.Get(), stroke);
    auto round = RoundStroke();
    for (int i = 0; i < 8; i++)
        Ray(t, cx, cy, Pi / 4 * i + hover * Pi / 8, r * 1.45f, r * (1.95f + 0.3f * hover), b.Get(), stroke * (1 + 0.3f * hover), round.Get());
}

// A laptop whose screen shows the screen settings: as bright as Brightness, and tinted from a calm blue (no filter) to
// warm yellow (the blue light filter at full). Hovering lifts the screen's light into a soft glow.
void DrawLaptop(ID2D1RenderTarget* t, const D2D1_RECT_F& box, float hover, float bright, float warm, int line, float stroke)
{
    float s = box.right - box.left, x = box.left, y = box.top;
    D2D1_RECT_F screen{x + s * 0.17f, y + s * 0.2f, x + s * 0.83f, y + s * 0.64f};
    float level = 0.3f + 0.7f * std::clamp(bright, 0.0f, 1.0f) + 0.12f * hover;
    int tint = Mix(Palette::Argb(96, 156, 228), Palette::Argb(255, 198, 92), warm);
    auto channel = [&](int sh) { return std::min(255, static_cast<int>(((tint >> sh) & 0xFF) * level)); };
    int shown = Palette::Argb(channel(16), channel(8), channel(0));
    Glow(t, (screen.left + screen.right) / 2, (screen.top + screen.bottom) / 2, s * 0.5f, tint, 0.35f * hover * level);
    D2D1_ROUNDED_RECT rr{screen, s * 0.05f, s * 0.05f};
    t->FillRoundedRectangle(rr, Brush(t, shown).Get());
    auto ink = Brush(t, line);
    t->DrawRoundedRectangle(rr, ink.Get(), stroke);
    // A faint sheen across the screen.
    float sw = screen.right - screen.left;
    ComPtr<ID2D1PathGeometry> sheen;
    ComPtr<ID2D1GeometrySink> sink;
    gfx::D2D()->CreatePathGeometry(sheen.GetAddressOf());
    sheen->Open(sink.GetAddressOf());
    sink->BeginFigure({screen.left + sw * 0.08f, screen.top + stroke}, D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine({screen.left + sw * 0.38f, screen.top + stroke});
    sink->AddLine({screen.left + sw * 0.18f, screen.bottom - stroke});
    sink->AddLine({screen.left + stroke * 0.5f, screen.bottom - stroke});
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    t->FillGeometry(sheen.Get(), Brush(t, White, (40 + 30 * hover) / 255.0f).Get());
    auto round = RoundStroke();
    float base = screen.bottom + s * 0.07f;
    t->DrawLine({x + s * 0.07f, base}, {x + s * 0.93f, base}, ink.Get(), stroke, round.Get());
    t->DrawLine({screen.left + s * 0.02f, screen.bottom + stroke * 0.5f}, {x + s * 0.12f, base - stroke * 0.5f}, ink.Get(), stroke, round.Get());
    t->DrawLine({screen.right - s * 0.02f, screen.bottom + stroke * 0.5f}, {x + s * 0.88f, base - stroke * 0.5f}, ink.Get(), stroke, round.Get());
}

} // namespace

void DrawIcon(ID2D1RenderTarget* t, const D2D1_RECT_F& box, int kind, float hover, float bright, float warm)
{
    float s = box.right - box.left, cx = box.left + s / 2, cy = box.top + s / 2, stroke = std::max(1.2f, s * 0.055f);
    constexpr int line = Palette::Argb(225, 232, 238);
    if (kind == Lamp) DrawLamp(t, box, hover, bright, line, stroke);
    else if (kind == Sun) DrawSun(t, cx, cy, s, hover, line, stroke);
    else if (kind == Laptop) DrawLaptop(t, box, hover, bright, warm, line, stroke);
    else {
        // The eye: shut and nearly gone while left alone; it opens when you come near.
        float ew = s * 0.78f, eh = ew * 0.6f;
        EyeArt::Draw(t, {cx - ew / 2, cy - eh / 2, cx + ew / 2, cy + eh / 2}, 0.06f + 0.94f * hover, 0, 0, stroke, 0.18f + 0.82f * hover);
    }
}

void DrawSymbol(ID2D1RenderTarget* t, const D2D1_RECT_F& box)
{
    float s = box.right - box.left, x = box.left, y = box.top, stroke = std::max(1.2f, s * 0.075f);
    auto fg = Brush(t, Theme::Fg);
    t->DrawRoundedRectangle({{x + s * 0.06f, y + s * 0.14f, x + s * 0.94f, y + s * 0.86f}, s * 0.12f, s * 0.12f}, fg.Get(), stroke);
    t->FillRoundedRectangle({{x + s * 0.5f, y + s * 0.5f, x + s * 0.84f, y + s * 0.76f}, s * 0.06f, s * 0.06f}, Brush(t, Theme::Accent()).Get());
    auto round = RoundStroke();
    float ax = x + s * 0.2f, ay = y + s * 0.28f, bx = x + s * 0.4f, by = y + s * 0.46f;
    t->DrawLine({ax, ay}, {bx, by}, fg.Get(), stroke, round.Get());
    t->DrawLine({bx - s * 0.15f, by}, {bx, by}, fg.Get(), stroke, round.Get());
    t->DrawLine({bx, by}, {bx, by - s * 0.15f}, fg.Get(), stroke, round.Get());
}

} // namespace bb::MiniArt
