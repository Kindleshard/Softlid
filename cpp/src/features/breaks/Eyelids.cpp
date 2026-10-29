#include "features/breaks/Eyelids.h"
#include "ui/PixelSurface.h"
#include <algorithm>
#include <cmath>

namespace bb {
namespace {

constexpr UINT_PTR FrameTimer = 1;
constexpr int FrameMs = 15, CloseMs = 260, HoldMs = 90, OpenMs = 420;

double Smooth(double x) { return x * x * (3 - 2 * x); }   // smooth start and stop, like a real lid

std::vector<RECT> Screens()
{
    std::vector<RECT> list;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR, HDC, LPRECT r, LPARAM p) {
        reinterpret_cast<std::vector<RECT>*>(p)->push_back(*r);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&list));
    return list;
}

} // namespace

// One lid: a click-through, never-focused, per-pixel-alpha window half a screen tall plus its soft rim.
class Eyelids::Lid : public Window {
public:
    Lid(const RECT& screen, bool top) : screen(screen), top(top)
    {
        int sw = screen.right - screen.left, sh = screen.bottom - screen.top;
        half = sh / 2;
        rim = std::max(24, sh / 9);
        Create(L"Softlid.Lid", WS_POPUP, PopupExStyle | WS_EX_LAYERED | WS_EX_TRANSPARENT, {screen.left, Y(0), screen.right, Y(0) + half + rim});
        // Drawn once and handed to Windows, which keeps its own copy: the pixels here go at once.
        PixelSurface art;
        Draw(art, sw);
        art.Present(hwnd, {screen.left, Y(0)}, sw, half + rim);
    }

    void ShowAbove()
    {
        if (!IsWindowVisible(hwnd)) ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    void MoveTo(double closed)
    {
        if (IsWindowVisible(hwnd)) SetWindowPos(hwnd, nullptr, screen.left, Y(closed), 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

private:
    // Out of sight at 0; at 1 the dark part reaches the middle and the rim spills just past it.
    int Y(double closed) const
    {
        int travel = half + rim;
        return top ? screen.top - travel + static_cast<int>(std::lround(closed * travel)) : screen.bottom - static_cast<int>(std::lround(closed * travel));
    }

    // Near-black (a lid lets a little warm light through), with an edge that dips toward the middle like a real lid
    // and fades out over the rim.
    void Draw(PixelSurface& art, int w)
    {
        int h = half + rim;
        art.Ensure(w, h);
        double curve = rim * 0.55;   // how much lower the edge sits in the middle than at the sides
        for (int y = 0; y < h; y++) {
            uint32_t* row = art.Row(y);
            for (int x = 0; x < w; x++) {
                double u = 2.0 * x / std::max(1, w - 1) - 1, edge = half + curve * (1 - u * u) - curve;   // the edge, from the lid's base
                double d = (top ? y : h - 1 - y) - edge;   // > 0 past the edge
                double a = d <= 0 ? 1 : d >= rim ? 0 : 1 - Smooth(d / rim);
                auto alpha = static_cast<uint32_t>(std::lround(250 * a));
                row[x] = (alpha << 24) | ((8 * alpha / 255) << 16) | ((6 * alpha / 255) << 8) | (5 * alpha / 255);   // (8, 6, 5), premultiplied
            }
        }
    }

    RECT screen;
    bool top;
    int half, rim;
};

Eyelids::Eyelids() { Create(L"Softlid.Eyelids", WS_POPUP, WS_EX_TOOLWINDOW, {0, 0, 0, 0}); }

Eyelids::~Eyelids()
{
    lids.clear();
    Destroy();
}

void Eyelids::Play(const Settings& s)
{
    depth = std::min(1.0, s.BlinkFadeDepth / 100.0);   // 100% (the slider's end) closes them all the way
    std::vector<RECT> now = Screens();
    bool same = now.size() == screens.size() &&
                std::equal(now.begin(), now.end(), screens.begin(), [](const RECT& a, const RECT& b) { return EqualRect(&a, &b) != FALSE; });
    if (!same) {
        lids.clear();
        screens = now;
        for (const RECT& r : screens) {
            lids.push_back(std::make_unique<Lid>(r, true));
            lids.push_back(std::make_unique<Lid>(r, false));
        }
    }
    started = GetTickCount64();
    Place(0);
    for (auto& lid : lids) lid->ShowAbove();
    SetTimer(hwnd, FrameTimer, FrameMs, nullptr);
}

void Eyelids::Step()
{
    double t = static_cast<double>(GetTickCount64() - started), c;
    if (t < CloseMs) c = Smooth(t / CloseMs);
    else if (t < CloseMs + HoldMs) c = 1;
    else if (t < CloseMs + HoldMs + OpenMs) c = 1 - Smooth((t - CloseMs - HoldMs) / OpenMs);
    else {
        KillTimer(hwnd, FrameTimer);
        lids.clear();   // (made again for the next blink: a lid the size of a screen is several MB)
        screens.clear();
        return;
    }
    Place(c * depth);
}

void Eyelids::Place(double closed)
{
    for (auto& lid : lids) lid->MoveTo(closed);
}

LRESULT Eyelids::OnMessage(UINT message, WPARAM w, LPARAM l)
{
    if (message == WM_TIMER && w == FrameTimer) {
        Step();
        return 0;
    }
    return Window::OnMessage(message, w, l);
}

} // namespace bb
