#include "ui/controls/Basic.h"
#include "core/Lang.h"
#include "ui/Theme.h"
#include <algorithm>
#include <cmath>
#include <wincodec.h>

#pragma comment(lib, "windowscodecs.lib")

namespace bb::ui {
namespace {

constexpr int ThumbFill = Palette::Argb(69, 69, 69), ThumbEdge = Palette::Argb(85, 85, 85);
constexpr int Amber = Palette::Argb(255, 184, 64), Icy = Palette::Argb(200, 230, 255);
constexpr int GreyInk = Palette::Argb(115, 115, 115), Muted = Palette::Argb(120, 120, 120);

// The round thumb the sliders share: a grey disc with a coloured centre that shrinks while dragged and grows under
// the pointer or with the keyboard.
void Thumb(Canvas& c, float x, float y, float r, int tint, bool dragging, bool lit)
{
    c.FillCircle(x, y, r, ThumbFill);
    c.OutlineCircle(x, y, r, ThumbEdge);
    c.FillCircle(x, y, r * (dragging ? 0.42f : lit ? 0.62f : 0.52f), tint);
}

std::vector<std::wstring> Translated(std::vector<std::wstring> english)
{
    for (auto& s : english) s = Lang::T(s);
    return english;
}

} // namespace

// ---- Label ----

Label::Label(std::wstring_view english, IDWriteTextFormat* font, int color)
    : font(font ? font : Fonts::Body()), text(english.empty() ? L"" : Lang::T(english)), color(color ? color : Theme::Fg)
{
}

void Label::SetText(std::wstring_view english) { SetPlainText(Lang::T(english)); }

void Label::SetPlainText(std::wstring t)
{
    if (t == text) return;
    text = std::move(t);
    Invalidate();
}

void Label::SetColor(int argb)
{
    if (argb == color) return;
    color = argb;
    Invalidate();
}

void Label::Paint(Canvas& c) { c.Text(text, font, {0, 0, W(), H()}, Enabled() ? color : Theme::Dim, align, wrap, top); }

void Icon::Set(wchar_t g, int argb)
{
    if (g == glyph && argb == color) return;
    glyph = g;
    color = argb;
    Invalidate();
}

// ---- IconImage ----

IconImage::~IconImage()
{
    if (icon) DestroyIcon(icon);
}

void IconImage::Paint(Canvas& c)
{
    if (!icon) return;
    ID2D1RenderTarget* t = c.Target();
    if (!bitmap || madeFor != t) {
        // Through WIC: the icon's pixels, with their transparency, as a Direct2D bitmap.
        gfx::ComPtr<IWICImagingFactory> wic;
        gfx::ComPtr<IWICBitmap> source;
        gfx::ComPtr<IWICFormatConverter> converted;
        bitmap.Reset();
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
            SUCCEEDED(wic->CreateBitmapFromHICON(icon, source.GetAddressOf())) &&
            SUCCEEDED(wic->CreateFormatConverter(converted.GetAddressOf())) &&
            SUCCEEDED(converted->Initialize(source.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                            WICBitmapPaletteTypeCustom)))
            t->CreateBitmapFromWicBitmap(converted.Get(), nullptr, bitmap.GetAddressOf());
        madeFor = t;
    }
    if (!bitmap) return;
    // Its pixels as they are (sharp), in units at this scale.
    float dpiX, dpiY;
    t->GetDpi(&dpiX, &dpiY);
    D2D1_SIZE_U px = bitmap->GetPixelSize();
    float w = px.width * 96 / dpiX, h = px.height * 96 / dpiY;
    t->DrawBitmap(bitmap.Get(), Rect((W() - w) / 2, (H() - h) / 2, w, h));
}

// ---- Button ----

Button::Button(std::wstring_view english) : text(english.empty() ? L"" : Lang::T(english))
{
    cursor = Cursor::Hand;
    focusable = true;
}

void Button::SetText(std::wstring_view english)
{
    text = Lang::T(english);
    glyph = 0;
    Invalidate();
}

void Button::SetGlyph(wchar_t g, float points)
{
    glyph = g;
    glyphPoints = points;
    text.clear();
    Invalidate();
}

void Button::SetTint(int argb)
{
    tint = argb;
    Invalidate();
}

void Button::Paint(Canvas& c)
{
    D2D1_RECT_F r{0.5f, 0.5f, W() - 1, H() - 1};
    int fill = down ? Theme::ButtonDown : hover ? Theme::ButtonHover : Theme::Button;
    int edge = Focused() ? Theme::Accent() : Theme::ButtonEdge, ink = Enabled() ? Theme::Fg : GreyInk;
    if (tint) {
        // Tinted (e.g. orange for an update): a dark wash of the colour, its edge and its text.
        fill = Mix(fill, tint, (down ? 40 : hover ? 70 : 52) / 255.0f);
        edge = ink = tint;
    }
    float radius = Height(r) / 2;
    c.FillRound(r, radius, fill);
    c.OutlineRound(r, radius, edge);
    if (glyph) {
        c.Glyph(glyph, Fonts::Glyphs(glyphPoints), {0, 0, W(), H()}, ink);
        return;
    }
    if (!icon) {
        c.Text(text, Fonts::Body(), {0, 0, W(), H()}, ink, HAlign::Center);
        return;
    }
    // A picture and words, centred together.
    float tw = text.empty() ? 0 : TextWidth(text, Fonts::Body()), space = text.empty() ? 0 : H() / 4;
    float x = (W() - iconSize - space - tw) / 2;
    icon(c, Rect(x, (H() - iconSize) / 2, iconSize, iconSize));
    if (!text.empty()) c.Text(text, Fonts::Body(), {x + iconSize + space, 0, W(), H()}, ink);
}

void Button::MouseDown(D2D1_POINT_2F)
{
    down = true;
    Invalidate();
}

void Button::MouseUp(D2D1_POINT_2F)
{
    down = false;
    Invalidate();
}

void Button::Click()
{
    if (onClick) onClick();
}

bool Button::Key(UINT key)
{
    if (key != VK_SPACE && key != VK_RETURN) return false;
    Click();
    return true;
}

// ---- Toggle ----

Toggle::Toggle(bool on) : on(on)
{
    cursor = Cursor::Hand;
    focusable = true;
}

void Toggle::SetOn(bool value)
{
    if (value == on) return;
    on = value;
    Invalidate();
}

void Toggle::Paint(Canvas& c)
{
    D2D1_RECT_F r{1, 1, W() - 2, H() - 2};
    float h = Height(r);
    // Greyed out when it can't be used (and then always shown as off).
    bool shownOn = on && Enabled();
    int line = Enabled() ? Theme::Dim : Theme::Line;
    if (shownOn) c.FillRound(r, h / 2, Theme::Accent());
    else c.OutlineRound(r, h / 2, line, std::max(1.0f, H() / 22));
    float kr = h / 2 - H() * 0.2f, cy = r.top + h / 2, cx = shownOn ? r.right - h / 2 : r.left + h / 2;
    c.FillCircle(cx, cy, kr, shownOn ? Palette::Argb(0, 0, 0) : line);
}

void Toggle::Click()
{
    SetOn(!on);
    if (onChange) onChange(on);
}

bool Toggle::Key(UINT key)
{
    if (key != VK_SPACE) return false;
    Click();
    return true;
}

// ---- Slider ----

Slider::Slider(int min, int max, int value) : min(min), max(max), value(std::clamp(value, min, max))
{
    cursor = Cursor::Hand;
    focusable = true;
}

void Slider::SetValue(int v)
{
    v = std::clamp(v, min, max);
    if (v == value) return;
    value = v;
    Invalidate();
}

void Slider::SetRange(int newMin, int newMax)
{
    min = newMin;
    max = newMax;
    Change(value);   // kept within the new range
    Invalidate();
}

void Slider::SetMarker(int v)
{
    if (v == marker) return;
    marker = v;
    Invalidate();
}

void Slider::SetMuted(bool on)
{
    if (on == muted) return;
    muted = on;
    Invalidate();
}

void Slider::Change(int v)
{
    v = std::clamp(v, min, max);
    if (v == value) return;
    value = v;
    Invalidate();
    if (onChange) onChange(value);
}

float Slider::Start() const { return H() * 0.3f + 1; }
float Slider::End() const { return W() - H() * 0.3f - 1; }
float Slider::X(int v) const { return Start() + (End() - Start()) * (v - min) / std::max(1, max - min); }

int Slider::Tint() const
{
    if (!warm) return Theme::Accent();
    // Warmer than daylight toward amber; cooler (below 0, when allowed) toward an icy blue-white.
    float k = value >= 0 ? static_cast<float>(value) / std::max(1, max) : static_cast<float>(value) / std::min(-1, min);
    k = k * k * (3 - 2 * k);
    return Mix(Theme::Accent(), value >= 0 ? Amber : Icy, k);
}

void Slider::Paint(Canvas& c)
{
    float cy = H() / 2, x = X(value), r = H() * 0.3f, th = std::max(2.0f, H() * 0.13f);
    c.Line(x, cy, End(), cy, Theme::Track, th);
    int tint = muted ? Muted : Tint();
    if (marker >= min && marker <= max) {
        // A mark on the track (the night schedule's strength), drawn under the filled part's end and the thumb.
        float mx = X(marker), mr = std::max(3.0f, th * 1.25f);
        c.FillCircle(mx, cy, mr, muted ? Theme::Track : Theme::Good);
        c.OutlineCircle(mx, cy, mr, Theme::Bg, std::max(1.0f, th * 0.4f));
    }
    if (x - Start() > 1) {
        if (warm && !muted) {
            // Warm: the filled part runs from blue to the colour the screen is being warmed towards.
            ID2D1RenderTarget* t = c.Target();
            D2D1_GRADIENT_STOP stops[] = {{0, gfx::Color(Theme::Accent())}, {1, gfx::Color(tint)}};
            gfx::ComPtr<ID2D1GradientStopCollection> collection;
            gfx::ComPtr<ID2D1LinearGradientBrush> brush;
            t->CreateGradientStopCollection(stops, 2, collection.GetAddressOf());
            t->CreateLinearGradientBrush({{Start() - 1, 0}, {x + 1, 0}}, collection.Get(), brush.GetAddressOf());
            gfx::ComPtr<ID2D1StrokeStyle> round;
            D2D1_STROKE_STYLE_PROPERTIES props{};
            props.startCap = props.endCap = D2D1_CAP_STYLE_ROUND;
            gfx::D2D()->CreateStrokeStyle(props, nullptr, 0, round.GetAddressOf());
            t->DrawLine({Start(), cy}, {x, cy}, brush.Get(), th, round.Get());
        } else {
            c.Line(Start(), cy, x, cy, muted ? tint : Theme::Accent(), th);
        }
    }
    Thumb(c, x, cy, r, tint, dragging, hover || Focused());
}

void Slider::MouseDown(D2D1_POINT_2F p)
{
    dragging = true;
    MouseMove(p, true);
    Invalidate();
}

void Slider::MouseMove(D2D1_POINT_2F p, bool down)
{
    if (down && dragging) Change(min + static_cast<int>(std::lround((p.x - Start()) / (End() - Start()) * (max - min))));
}

void Slider::MouseUp(D2D1_POINT_2F)
{
    dragging = false;
    Invalidate();
}

// Only once clicked into: otherwise the wheel scrolls the page.
bool Slider::Wheel(int delta)
{
    if (!Focused()) return false;
    Change(value + (delta > 0 ? 1 : -1) * std::max(1, (max - min) / 40));
    return true;
}

bool Slider::Key(UINT key)
{
    if (key == VK_LEFT || key == VK_DOWN) Change(value - 1);
    else if (key == VK_RIGHT || key == VK_UP) Change(value + 1);
    else return false;
    return true;
}

// ---- StepSlider ----

StepSlider::StepSlider(std::vector<std::wstring> englishLabels) : labels(Translated(std::move(englishLabels)))
{
    cursor = Cursor::Hand;
    focusable = true;
}

void StepSlider::SetSelected(int i)
{
    if (i == selected) return;
    selected = i;
    Invalidate();
}

void StepSlider::SetSteps(std::vector<std::wstring> englishLabels)
{
    labels = Translated(std::move(englishLabels));
    if (selected >= static_cast<int>(labels.size())) selected = static_cast<int>(labels.size()) - 1;
    Invalidate();
}

void StepSlider::Change(int i)
{
    if (i == selected) return;
    selected = i;
    Invalidate();
    if (onChange) onChange(i);
}

// Room at each end for half of the widest label, so the outer labels aren't cut off.
float StepSlider::Pad() const
{
    float widest = 0;
    for (auto& l : labels) widest = std::max(widest, TextWidth(l, Fonts::Small()));
    return std::max(H() * 0.3f, widest / 2 + 2);
}

float StepSlider::X(int i) const
{
    float pad = Pad();
    return pad + (W() - 2 * pad) * i / std::max(1, static_cast<int>(labels.size()) - 1);
}

int StepSlider::Nearest(float x) const
{
    int best = 0;
    for (int i = 1; i < static_cast<int>(labels.size()); i++)
        if (std::abs(X(i) - x) < std::abs(X(best) - x)) best = i;
    return best;
}

void StepSlider::Paint(Canvas& c)
{
    if (labels.empty()) return;
    int last = static_cast<int>(labels.size()) - 1;
    float y = H() * 0.3f, th = std::max(2.0f, H() * 0.06f), r = H() * 0.12f;
    c.Line(X(0), y, X(last), y, Theme::Track, th, false);
    if (selected > 0) c.Line(X(0), y, X(selected), y, Theme::Accent(), th, false);
    for (int i = 0; i <= last; i++) {
        float x = X(i);
        bool reached = selected >= 0 && i <= selected;
        c.FillCircle(x, y, r, reached ? Theme::Accent() : Theme::Track);
        if (i == selected) Thumb(c, x, y, H() * 0.2f, Theme::Accent(), dragging, Focused());
        float tw = TextWidth(labels[i], Fonts::Small());
        c.Text(labels[i], Fonts::Small(), Rect(x - tw / 2 - 1, H() * 0.56f, tw + 2, H() * 0.44f), i == selected ? Theme::Fg : Theme::Dim,
               HAlign::Center);
    }
}

void StepSlider::MouseDown(D2D1_POINT_2F p)
{
    dragging = true;
    Change(Nearest(p.x));
    Invalidate();
}

void StepSlider::MouseMove(D2D1_POINT_2F p, bool down)
{
    if (down && dragging) Change(Nearest(p.x));
}

void StepSlider::MouseUp(D2D1_POINT_2F)
{
    dragging = false;
    Invalidate();
}

bool StepSlider::Key(UINT key)
{
    if (key == VK_LEFT && selected > 0) Change(selected - 1);
    else if (key == VK_RIGHT && selected < static_cast<int>(labels.size()) - 1) Change(selected + 1);
    else return false;
    return true;
}

// ---- ProgressLine ----

void ProgressLine::SetProgress(float p)
{
    p = std::clamp(p, 0.0f, 1.0f);
    if (p == progress) return;
    progress = p;
    Invalidate();
}

void ProgressLine::Paint(Canvas& c)
{
    c.FillRound({0, 0, W(), H()}, H() / 2, Theme::Line);
    float w = W() * progress;
    if (w >= H()) c.FillRound({0, 0, w, H()}, H() / 2, Theme::Accent());
}

} // namespace bb::ui
