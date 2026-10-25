#include "features/screen/ColorFilter.h"
#include "core/ColorMath.h"
#include "core/Night.h"
#include "platform/Window.h"
#include <magnification.h>
#include <algorithm>

#pragma comment(lib, "magnification.lib")

namespace bb {

// A see-through coloured sheet over one monitor (the fallback): an overlay can only blend, so it dims with black and
// warms with amber, folded into one colour and one opacity.
class ColorFilter::Overlay : public Window {
public:
    explicit Overlay(const RECT& monitor)
    {
        Create(L"BlinkBuddy.Overlay", WS_POPUP, PopupExStyle | WS_EX_LAYERED | WS_EX_TRANSPARENT, monitor);
    }
    void Set(COLORREF c, float opacity)
    {
        color = c;
        SetLayeredWindowAttributes(hwnd, 0, static_cast<BYTE>(std::clamp(opacity, 0.01f, 1.0f) * 255), LWA_ALPHA);
        InvalidateRect(hwnd, nullptr, TRUE);
        if (!IsWindowVisible(hwnd)) ShowOnTop(hwnd);
    }

private:
    LRESULT OnMessage(UINT message, WPARAM w, LPARAM l) override
    {
        if (message == WM_ERASEBKGND) {
            RECT r;
            GetClientRect(hwnd, &r);
            HBRUSH brush = CreateSolidBrush(color);
            FillRect(reinterpret_cast<HDC>(w), &r, brush);
            DeleteObject(brush);
            return 1;
        }
        return Window::OnMessage(message, w, l);
    }
    COLORREF color = 0;
};

ColorFilter::ColorFilter() = default;
ColorFilter::~ColorFilter() { Off(); }

void ColorFilter::Apply(const Settings& s, float night, float prescale, float brightness)
{
    bool on = s.FilterEnabled;
    float bright = (on ? brightness : 1.0f) * prescale;
    bool invert = on && s.Invert;
    int effect = on ? s.ColorEffect : ColorMath::NoEffect;
    // A colour effect and the blue light filter take turns: while an effect is on, its colours aren't warmed (Sepia
    // and Red night already have next to no blue; a warmed Grayscale or E-ink just turns muddy).
    double warm = on ? Night::Blue(s, night) : 0, blue = effect == ColorMath::NoEffect ? warm : 0;
    ColorMath::Rgb k;
    if (blue != 0) k = ColorMath::KelvinToRgb(ColorMath::Kelvin(blue));   // below 0: cooler than daylight
    bool neutral = bright >= 1 && k.r >= 1 && k.g >= 1 && k.b >= 1 && !invert && effect == ColorMath::NoEffect;

    if (!magFailed) {
        if (neutral) { StopMagnifier(); return; }
        if (!magOn) magOn = MagInitialize() != FALSE;
        ColorMath::Matrix m = ColorMath::Build({k.r * bright, k.g * bright, k.b * bright}, invert, effect, s.GrayStyle);
        MAGCOLOREFFECT e{};
        std::copy(m.begin(), m.end(), &e.transform[0][0]);
        if (magOn && MagSetFullscreenColorEffect(&e)) return;
        StopMagnifier();
        magFailed = true;
    }
    // An overlay can't cool, invert or do the effects, so the blue light filter is back on here.
    ApplyOverlays(bright >= 1 && warm <= 0, bright, static_cast<float>(std::max(0.0, warm) / 100));
}

void ColorFilter::Off()
{
    StopMagnifier();
    overlays.clear();
}

void ColorFilter::StopMagnifier()
{
    if (!magOn) return;
    MAGCOLOREFFECT identity{};
    identity.transform[0][0] = identity.transform[1][1] = identity.transform[2][2] = identity.transform[3][3] = identity.transform[4][4] = 1;
    MagSetFullscreenColorEffect(&identity);
    MagUninitialize();
    magOn = false;
}

void ColorFilter::ApplyOverlays(bool neutral, float bright, float warmth)
{
    if (neutral) { overlays.clear(); return; }
    std::vector<RECT> monitors;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR, HDC, LPRECT r, LPARAM list) {
        reinterpret_cast<std::vector<RECT>*>(list)->push_back(*r);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&monitors));
    if (overlays.size() != monitors.size()) {
        overlays.clear();
        for (const RECT& m : monitors) overlays.push_back(std::make_unique<Overlay>(m));
    }
    float d = 1 - bright, t = warmth * 0.45f;
    float a = 1 - (1 - d) * (1 - t);
    float amber = a > 0.001f ? (1 - d) * t / a : 0;
    COLORREF color = RGB(static_cast<int>(255 * amber), static_cast<int>(120 * amber), 0);
    for (auto& o : overlays) o->Set(color, a);
}

} // namespace bb
