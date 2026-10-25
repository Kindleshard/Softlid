#include "features/tone/Comfort.h"
#include "core/ToneCurve.h"
#include <algorithm>
#include <cmath>

namespace bb {
namespace {

constexpr UINT_PTR SampleTimer = 1, EaseTimer = 2;
constexpr double MaxExtraDim = 0.15;

} // namespace

Comfort::Comfort() { Create(L"BlinkBuddy.Comfort", WS_POPUP, WS_EX_TOOLWINDOW, {0, 0, 0, 0}); }   // (hidden: for the timers)
Comfort::~Comfort() { Destroy(); }

double Comfort::White() const { return ToneCurve::White(level) * adapt; }
double Comfort::Scale() const { return std::pow(White(), 1 / ToneCurve::Gamma(level)); }
double Comfort::Net(double x) const { return std::pow(Scale() * x, ToneCurve::Gamma(level)); }

void Comfort::Apply(const Settings& s, bool active)
{
    level = s.Level();
    bool run = active && s.AdaptToContent;
    if (run && !sampling) SetTimer(hwnd, SampleTimer, 2000, nullptr);
    if (!run && sampling) KillTimer(hwnd, SampleTimer);
    sampling = run;
    if (!run) {
        if (easing) KillTimer(hwnd, EaseTimer);
        easing = false;
        adapt = target = 1;
    }
}

void Comfort::Sample()
{
    double mean = ScreenMean() / std::max(0.05f, currentGain ? currentGain() : 1.0f);
    target = 1 - MaxExtraDim * std::clamp((mean - 0.35) / 0.45, 0.0, 1.0);
    if (std::abs(target - adapt) > 0.01 && !easing) {
        easing = true;
        SetTimer(hwnd, EaseTimer, 50, nullptr);
    }
}

void Comfort::Ease()
{
    adapt += std::clamp(target - adapt, -0.01, 0.01);
    if (std::abs(target - adapt) < 0.001) {
        adapt = target;
        KillTimer(hwnd, EaseTimer);
        easing = false;
    }
    if (changed) changed();
}

// Average brightness (0..1) of the main screen from a point-sampled 32x20 thumbnail.
double Comfort::ScreenMean() const
{
    constexpr int w = 32, h = 20;
    HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
    void* bits = nullptr;
    HBITMAP thumb = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(dc, thumb);
    SetStretchBltMode(dc, COLORONCOLOR);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    RECT b = mi.rcMonitor;
    StretchBlt(dc, 0, 0, w, h, screen, b.left, b.top, b.right - b.left, b.bottom - b.top, SRCCOPY);
    GdiFlush();
    double sum = 0;
    auto* px = static_cast<const UINT32*>(bits);
    for (int i = 0; px && i < w * h; i++) sum += 0.2126 * ((px[i] >> 16) & 255) + 0.7152 * ((px[i] >> 8) & 255) + 0.0722 * (px[i] & 255);
    SelectObject(dc, old);
    DeleteObject(thumb);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return sum / (255.0 * w * h);
}

LRESULT Comfort::OnMessage(UINT message, WPARAM w, LPARAM l)
{
    if (message == WM_TIMER) {
        if (w == SampleTimer) Sample();
        else if (w == EaseTimer) Ease();
        return 0;
    }
    return Window::OnMessage(message, w, l);
}

} // namespace bb
