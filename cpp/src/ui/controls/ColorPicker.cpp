#include "ui/controls/ColorPicker.h"
#include "ui/DirectWindow.h"
#include "ui/Theme.h"
#include "ui/controls/Basic.h"
#include "ui/controls/Pickers.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace bb::ui {

// ---- Hsv ----

int Hsv::ToArgb(double h, double s, double v)
{
    h = std::fmod(std::fmod(h, 360) + 360, 360) / 60;
    int i = static_cast<int>(std::floor(h));
    double f = h - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f)), r, g, b;
    switch (i) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
    auto byte = [](double x) { return static_cast<int>(std::lround(x * 255)); };
    return Palette::Argb(byte(r), byte(g), byte(b));
}

void Hsv::FromArgb(int argb, double& h, double& s, double& v)
{
    double r = ((argb >> 16) & 0xFF) / 255.0, g = ((argb >> 8) & 0xFF) / 255.0, b = (argb & 0xFF) / 255.0;
    double max = std::max({r, g, b}), min = std::min({r, g, b}), d = max - min;
    v = max;
    s = max <= 0 ? 0 : d / max;
    if (d <= 0) h = 0;
    else if (max == r) h = 60 * std::fmod((g - b) / d, 6);
    else if (max == g) h = 60 * ((b - r) / d + 2);
    else h = 60 * ((r - g) / d + 4);
    if (h < 0) h += 360;
}

// ---- The pop-up ----

// Hue round the wheel and saturation out from its middle, brightness on the bar under it, quick picks, and the colour
// as a hex code you can type. It closes when you click anywhere else (or press Enter or Esc).
class ColorPopup : public DirectWindow {
public:
    ColorPopup(int start, std::vector<int> presets, bool allowAccent, bool useAccent);
    int Value() const { return Hsv::ToArgb(hue, sat, val); }
    bool MatchAccent() const { return matchAccent; }
    std::function<void()> changed;

    double hue = 0, sat = 0, val = 0;
    bool matchAccent;

    void Pick(int argb, bool writeHex);   // from a quick pick or the hex box
    void Redraw(bool writeHex);

protected:
    bool Shortcut(UINT key) override;

private:
    TextBox* hex = nullptr;
    Button* accentButton = nullptr;
};

namespace {

// Hue round, saturation outwards, drawn at the bar's brightness; a ring marks the colour.
class Wheel : public Control {
public:
    explicit Wheel(ColorPopup& owner) : owner(owner) { cursor = Cursor::Cross; }

    void Paint(Canvas& c) override
    {
        float R = std::min(W(), H()) / 2 - 1, cx = W() / 2, cy = H() / 2;
        DrawDisc(c);
        if (owner.val < 1) c.FillCircle(cx, cy, R, Palette::Argb(0, 0, 0), static_cast<float>(1 - owner.val));
        double a = owner.hue * std::numbers::pi / 180;
        float mx = cx + static_cast<float>(std::cos(a) * owner.sat * R), my = cy - static_cast<float>(std::sin(a) * owner.sat * R);
        float m = H() * 0.045f;
        c.OutlineCircle(mx, my, m, Palette::Argb(0, 0, 0), 3, 160 / 255.0f);
        c.OutlineCircle(mx, my, m, Palette::Argb(255, 255, 255), 1.6f);
    }

    void MouseDown(D2D1_POINT_2F p) override { PickAt(p); }
    void MouseMove(D2D1_POINT_2F p, bool down) override { if (down) PickAt(p); }

private:
    // The full-brightness disc, made pixel by pixel once at the window's scale.
    void DrawDisc(Canvas& c)
    {
        ID2D1RenderTarget* t = c.Target();
        float dpiX, dpiY;
        t->GetDpi(&dpiX, &dpiY);
        UINT w = static_cast<UINT>(std::lround(W() * dpiX / 96)), h = static_cast<UINT>(std::lround(H() * dpiY / 96));
        if (!disc || madeFor != t || discW != w) {
            std::vector<UINT32> pixels(static_cast<size_t>(w) * h);
            float R = std::min(w, h) / 2.0f - dpiX / 96, cx = w / 2.0f, cy = h / 2.0f;
            for (UINT y = 0; y < h; y++)
                for (UINT x = 0; x < w; x++) {
                    double dx = x + 0.5 - cx, dy = cy - (y + 0.5), d = std::sqrt(dx * dx + dy * dy);
                    double alpha = std::clamp(R - d + 0.5, 0.0, 1.0);
                    if (alpha <= 0) continue;
                    int argb = Hsv::ToArgb(std::atan2(dy, dx) * 180 / std::numbers::pi, std::min(1.0, d / R), 1);
                    auto pm = [&](int shift) { return static_cast<UINT32>(((argb >> shift) & 0xFF) * alpha + 0.5); };   // premultiplied
                    pixels[static_cast<size_t>(y) * w + x] =
                        (static_cast<UINT32>(alpha * 255 + 0.5) << 24) | (pm(16) << 16) | (pm(8) << 8) | pm(0);
                }
            disc.Reset();
            auto props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpiX, dpiY);
            t->CreateBitmap(D2D1::SizeU(w, h), pixels.data(), w * 4, props, disc.GetAddressOf());
            madeFor = t;
            discW = w;
        }
        if (disc) t->DrawBitmap(disc.Get(), {0, 0, W(), H()});
    }

    void PickAt(D2D1_POINT_2F p)
    {
        float R = std::min(W(), H()) / 2 - 1;
        double dx = p.x - W() / 2.0, dy = H() / 2.0 - p.y;
        owner.hue = std::fmod(std::atan2(dy, dx) * 180 / std::numbers::pi + 360, 360);
        owner.sat = std::min(1.0, std::sqrt(dx * dx + dy * dy) / R);
        if (owner.val < 0.05) owner.val = 1;   // picking a hue on a black wheel: show it
        owner.matchAccent = false;
        owner.Redraw(true);
    }

    ColorPopup& owner;
    gfx::ComPtr<ID2D1Bitmap> disc;
    ID2D1RenderTarget* madeFor = nullptr;
    UINT discW = 0;
};

// Brightness: black on the left to the full colour on the right.
class ValueBar : public Control {
public:
    explicit ValueBar(ColorPopup& owner) : owner(owner) { cursor = Cursor::Hand; }

    void Paint(Canvas& c) override
    {
        float knob = H() - 2;
        D2D1_RECT_F track = Rect(knob / 2, H() * 0.2f, W() - knob, H() * 0.6f);
        ID2D1RenderTarget* t = c.Target();
        D2D1_GRADIENT_STOP stops[] = {{0, gfx::Color(Palette::Argb(0, 0, 0))}, {1, gfx::Color(Hsv::ToArgb(owner.hue, owner.sat, 1))}};
        gfx::ComPtr<ID2D1GradientStopCollection> collection;
        gfx::ComPtr<ID2D1LinearGradientBrush> brush;
        t->CreateGradientStopCollection(stops, 2, collection.GetAddressOf());
        t->CreateLinearGradientBrush({{track.left - 1, 0}, {track.right + 1, 0}}, collection.Get(), brush.GetAddressOf());
        t->FillRoundedRectangle({track, Height(track) / 2, Height(track) / 2}, brush.Get());
        float x = track.left + static_cast<float>(owner.val) * Width(track);
        c.FillCircle(x, 1 + knob / 2, knob / 2, owner.Value());
        c.OutlineCircle(x, 1 + knob / 2, knob / 2, Palette::Argb(255, 255, 255), 1.6f);
    }

    void MouseDown(D2D1_POINT_2F p) override { PickAt(p.x); }
    void MouseMove(D2D1_POINT_2F p, bool down) override { if (down) PickAt(p.x); }

private:
    void PickAt(float x)
    {
        float knob = H() - 2;
        owner.val = std::clamp((x - knob / 2) / (W() - knob), 0.0f, 1.0f);
        owner.matchAccent = false;
        owner.Redraw(true);
    }

    ColorPopup& owner;
};

// Quick picks in a row.
class Swatches : public Control {
public:
    Swatches(ColorPopup& owner, std::vector<int> colors) : owner(owner), colors(std::move(colors)) { cursor = Cursor::Hand; }

    void Paint(Canvas& c) override
    {
        float step = Step(), d = std::min(step, H()) - 6;
        int now = owner.Value();
        for (size_t i = 0; i < colors.size(); i++) {
            float cx = step * (i + 0.5f), cy = H() / 2;
            c.FillCircle(cx, cy, d / 2, colors[i]);
            c.OutlineCircle(cx, cy, d / 2, Palette::Argb(255, 255, 255), 1, 80 / 255.0f);
            bool on = (colors[i] & 0xFFFFFF) == (now & 0xFFFFFF) && !owner.matchAccent;
            if (on || static_cast<int>(i) == hoverIndex) c.OutlineCircle(cx, cy, d / 2 + 2.5f, on ? Theme::Fg : Theme::Dim, 1.5f);
        }
    }

    void MouseMove(D2D1_POINT_2F p, bool) override
    {
        int i = std::min(static_cast<int>(colors.size()) - 1, static_cast<int>(p.x / Step()));
        if (i != hoverIndex) {
            hoverIndex = i;
            Invalidate();
        }
    }

    void MouseLeave() override
    {
        hoverIndex = -1;
        Invalidate();
    }

    void Click() override
    {
        if (hoverIndex >= 0 && hoverIndex < static_cast<int>(colors.size())) owner.Pick(colors[hoverIndex], true);
    }

private:
    float Step() const { return W() / std::max<size_t>(1, colors.size()); }
    ColorPopup& owner;
    std::vector<int> colors;
    int hoverIndex = -1;
};

} // namespace

ColorPopup::ColorPopup(int start, std::vector<int> presets, bool allowAccent, bool useAccent) : matchAccent(useAccent)
{
    Hsv::FromArgb(start, hue, sat, val);
    Root().Add<Wheel>(*this).SetBounds(16, 16, 216, 216);
    Root().Add<ValueBar>(*this).SetBounds(16, 244, 216, 20);
    Root().Add<Swatches>(*this, std::move(presets)).SetBounds(14, 274, 220, 26);
    hex = &Root().Add<TextBox>(7, true);
    hex->SetBounds(16, 308, 96, 26);
    hex->onChange = [this](const std::wstring& text) {
        std::wstring t = text[0] == L'#' ? text.substr(1) : text;
        wchar_t* end = nullptr;
        unsigned long rgb = std::wcstoul(t.c_str(), &end, 16);
        if (t.size() == 6 && *end == L'\0') Pick(static_cast<int>(0xFF000000u | rgb), false);
    };
    hex->onKey = [this](UINT) { Close(); };
    if (allowAccent) {
        accentButton = &Root().Add<Button>(L"Match accent");
        accentButton->SetBounds(122, 305, 110, 32);
        accentButton->onClick = [this] {
            matchAccent = !matchAccent;
            if (matchAccent) Hsv::FromArgb(Theme::Accent(), hue, sat, val);
            Redraw(true);
        };
    }
}

void ColorPopup::Pick(int argb, bool writeHex)
{
    Hsv::FromArgb(argb, hue, sat, val);
    matchAccent = false;
    Redraw(writeHex);
}

void ColorPopup::Redraw(bool writeHex)
{
    InvalidateAll();
    if (accentButton) accentButton->SetTint(matchAccent ? Theme::Accent() : 0);
    if (writeHex) {
        int c = Value();
        wchar_t text[8];
        swprintf_s(text, L"#%02X%02X%02X", (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
        hex->SetText(text);
    }
    if (changed) changed();
}

bool ColorPopup::Shortcut(UINT key)
{
    if (key != VK_ESCAPE && key != VK_RETURN) return false;
    Close();
    return true;
}

// ---- ColorButton ----

ColorButton::ColorButton() : presets(std::begin(Palette::Accents), std::end(Palette::Accents))
{
    cursor = Cursor::Hand;
    focusable = true;
}

ColorButton::~ColorButton()
{
    if (!popup) return;
    popup->changed = nullptr;
    popup->onClosed = [p = popup] { delete p; };
    popup->Close();
}

void ColorButton::SetValue(int argb)
{
    value = argb;
    Invalidate();
}

void ColorButton::SetUseAccent(bool on)
{
    useAccent = on;
    Invalidate();
}

int ColorButton::Shown() const { return useAccent ? Theme::Accent() : value; }

void ColorButton::Paint(Canvas& c)
{
    float d = std::min(W(), H()) - 2, cx = W() / 2, cy = H() / 2;
    c.OutlineCircle(cx, cy, d / 2, hover || Focused() ? Theme::Fg : Palette::Argb(90, 90, 94), 1.5f);
    float inner = d / 2 - d * 0.14f;
    c.FillCircle(cx, cy, inner, Shown());
    c.OutlineCircle(cx, cy, inner, Palette::Argb(255, 255, 255), 1, 70 / 255.0f);   // so a dark colour still shows
}

bool ColorButton::Key(UINT key)
{
    if (key != VK_SPACE && key != VK_RETURN) return false;
    Click();
    return true;
}

void ColorButton::Click()
{
    DirectWindow* w = Window();
    if (!w || popup) return;
    popup = new ColorPopup(value, presets, allowAccent, useAccent);
    popup->Redraw(true);   // (fills in the hex box; before `changed` is set, so opening it changes nothing)
    popup->changed = [this] {
        value = popup->Value();
        useAccent = popup->MatchAccent();
        Invalidate();
        if (onChange) onChange();
    };
    popup->onClosed = [this, p = popup] {
        popup = nullptr;
        delete p;
        if (onChosen) onChosen();
    };
    // Under the swatch, kept on the screen.
    constexpr float pw = 248, ph = 344;
    D2D1_POINT_2F o = Origin();
    POINT below = w->ToScreen({o.x + W() / 2, o.y + H() + 4}), above = w->ToScreen({o.x, o.y - 4});
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorFromPoint(below, MONITOR_DEFAULTTONEAREST), &mi);
    RECT area = mi.rcWork;
    LONG wpx = static_cast<LONG>(pw * w->Scale()), hpx = static_cast<LONG>(ph * w->Scale());
    POINT at{std::max(area.left, std::min(area.right - wpx, below.x - wpx / 2)), below.y + hpx <= area.bottom ? below.y : above.y - hpx};
    DirectWindow::Options options;
    options.popup = true;
    options.closeOnDeactivate = true;
    options.owner = w->Handle();
    popup->Open(pw, ph, &at, options);
    popup->Show();
}

} // namespace bb::ui
