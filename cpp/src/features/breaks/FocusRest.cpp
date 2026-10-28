#include "features/breaks/FocusRest.h"
#include "core/Clock.h"
#include "core/Lang.h"
#include "features/reminders/BreakCard.h"
#include "features/reminders/Chime.h"
#include "ui/Access.h"
#include "ui/Theme.h"
#include <mmsystem.h>
#include <shellscalingapi.h>
#include <algorithm>
#include <cmath>
#include <numbers>

#pragma comment(lib, "winmm.lib")

namespace bb {
namespace {

using namespace ui;

constexpr UINT_PTR FrameTimer = 1;
constexpr int Back = Palette::Argb(13, 15, 19);
constexpr int Green = Palette::Argb(95, 208, 138), GreenTrack = Palette::Argb(31, 58, 44), Cue = Palette::Argb(201, 238, 215);
constexpr int Blue = Palette::Argb(76, 194, 255);
constexpr double FadeIn = 0.5, FadeOut = 0.6;

// One breath: in for 4 s, held for 1.5 s, out for 4.5 s. How full the circle is (0.72 .. 1), and which words go with it.
constexpr double In = 4, Hold = 1.5, Out = 4.5, Breath = In + Hold + Out;
const wchar_t* const Cues[] = {L"Breathe in...", L"Hold...", L"Breathe out..."};
int CueAt(double t)
{
    double p = std::fmod(t, Breath);
    return p < In ? 0 : p < In + Hold ? 1 : 2;
}
float Fullness(double t)
{
    double p = std::fmod(t, Breath), k;
    if (p < In) k = p / In;
    else if (p < In + Hold) k = 1;
    else k = 1 - (p - In - Hold) / Out;
    k = k * k * (3 - 2 * k);
    return static_cast<float>(0.72 + 0.28 * k);
}

} // namespace

// Everything on the screen but the buttons.
class FocusRest::Scene : public Control {
public:
    explicit Scene(FocusRest& owner) : owner(owner) {}

    float R() const { return std::clamp(std::min(W(), H()) * 0.17f, 90.0f, 220.0f); }
    float CX() const { return W() / 2; }
    float CY() const { return H() / 2 - 24; }
    D2D1_RECT_F RingBox() const { float r = R() + 8; return {CX() - r, CY() - r, CX() + r, CY() + r}; }
    D2D1_RECT_F CueBox() const { return {0, CY() + R() + 18, W(), CY() + R() + 52}; }

    void Paint(Canvas& c) override
    {
        double t = owner.Now();
        float r = R(), cx = CX(), cy = CY();
        // The rounds done so far, as dots, over it.
        c.Text(Lang::F(L"Focus round {0} done", {std::to_wstring(owner.round)}), Fonts::Get(11), {0, cy - r - 96, W(), cy - r - 70},
               Palette::Argb(138, 144, 156), HAlign::Center);
        int dots = std::clamp(owner.round, 1, 8);
        for (int i = 0; i < dots; i++) c.FillCircle(cx + (i - (dots - 1) / 2.0f) * 16, cy - r - 52, 4, Blue);
        // The ring: what is left of the rest, emptying clockwise from the top.
        double left = std::max(0.0, owner.total - std::max(0.0, t - FadeIn));
        c.OutlineCircle(cx, cy, r, GreenTrack, 6);
        gfx::ComPtr<ID2D1SolidColorBrush> b;
        c.Target()->CreateSolidColorBrush(gfx::Color(Green), b.GetAddressOf());
        gfx::Arc(c.Target(), cx, cy, r, -90, static_cast<float>(360 * left / owner.total), b.Get(), 6, gfx::RoundCaps().Get());
        // The breathing circle inside it.
        float k = Fullness(t);
        c.FillCircle(cx, cy, r * 0.82f * k, Green, 0.14f);
        c.FillCircle(cx, cy, r * 0.60f * k, Green, 0.14f);
        int seconds = static_cast<int>(std::ceil(left));
        c.Text(MinSec(seconds), Fonts::Get(30, gfx::Weight::Semibold), {cx - r, cy - 30, cx + r, cy + 18}, Theme::Fg, HAlign::Center);
        c.Text(Lang::T(L"Rest"), Fonts::Get(10), {cx - r, cy + 18, cx + r, cy + 40}, Palette::Argb(138, 144, 156), HAlign::Center);
        // What to do now, and what comes next.
        c.Text(Lang::T(Cues[CueAt(t)]), Fonts::Get(14), CueBox(), Cue, HAlign::Center);
        c.Text(Lang::F(L"Next: focus for {0} min", {std::to_wstring(owner.workMinutes)}), Fonts::Get(10),
               {0, CY() + R() + 54, W(), CY() + R() + 78}, Palette::Argb(138, 144, 156), HAlign::Center);
        c.Text(Lang::T(L"Stand up, stretch, and look far away"), Fonts::Get(10), {28, H() - 62, W() / 2, H() - 28},
               Palette::Argb(110, 116, 126));
    }

private:
    FocusRest& owner;
};

FocusRest::FocusRest(HWND owner, const Settings& s, int round)
    : owner(owner), sounds(s.Sounds), forced(s.ForceRest), round(round), workMinutes(s.FocusWork), total(s.FocusRest * 60.0)
{
    scene = &Root().Add<Scene>(*this);
    more = &Root().Add<Button>(L"+5 min");
    more->onClick = [this] {
        this->total += 300;
        InvalidateAll();
    };
    skip = &Root().Add<Button>(L"Skip rest");
    skip->onClick = [this] { Finish(); };
    more->SetVisible(!forced);
    skip->SetVisible(!forced);

    // The whole screen under the pointer, faded in.
    POINT cursor;
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(monitor, &mi);
    UINT dx = 96, dy = 96;
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dx, &dy);
    float sc = dx / 96.0f;
    Options opts;
    opts.popup = true;
    opts.edge = false;
    opts.background = Back;
    POINT at{mi.rcMonitor.left, mi.rcMonitor.top};
    Open((mi.rcMonitor.right - mi.rcMonitor.left) / sc, (mi.rcMonitor.bottom - mi.rcMonitor.top) / sc, &at, opts);
    SetWindowLongPtrW(Handle(), GWL_EXSTYLE, GetWindowLongPtrW(Handle(), GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TOPMOST);
    SetLayeredWindowAttributes(Handle(), 0, 0, LWA_ALPHA);
    SetWindowPos(Handle(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    Show(!CloakedCopy());   // (a cloaked diagnostic copy never takes the keyboard)
    Access::Announce(Handle(), Lang::F(L"Focus round {0} done", {std::to_wstring(round)}) + L". " +
                                   Lang::T(L"Stand up, stretch, and look far away"));
    timeBeginPeriod(1);
    if (sounds) Chime::Play(false);
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&clockStart);
    SetTimer(Handle(), FrameTimer, 33, nullptr);
}

FocusRest::~FocusRest()
{
    if (Handle()) KillTimer(Handle(), FrameTimer);
    timeEndPeriod(1);
}

void FocusRest::Layout()
{
    D2D1_SIZE_F size = ClientSize();
    if (scene) scene->SetBounds(0, 0, size.width, size.height);
    float h = 34, m = 28;
    if (skip) skip->SetBounds(size.width - m - 112, size.height - m - h, 112, h);
    if (more) more->SetBounds(size.width - m - 112 - 12 - 96, size.height - m - h, 96, h);
}

double FocusRest::Now() const
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart - clockStart.QuadPart) / frequency.QuadPart;
}

float FocusRest::Progress() const { return static_cast<float>(std::clamp(std::max(0.0, Now() - FadeIn) / total, 0.0, 1.0)); }
int FocusRest::SecondsLeft() const { return static_cast<int>(std::ceil(std::max(0.0, total - std::max(0.0, Now() - FadeIn)))); }

bool FocusRest::Shortcut(UINT key)
{
    if (key != VK_ESCAPE || forced) return false;
    Finish();
    return true;
}

LRESULT FocusRest::OnMessage(UINT message, WPARAM w, LPARAM l)
{
    if (message == WM_TIMER && w == FrameTimer) {
        Frame();
        return 0;
    }
    return DirectWindow::OnMessage(message, w, l);
}

void FocusRest::Finish()
{
    if (closeAt < 0) closeAt = Now();
}

// About 30 times a second: the fades, and the ring and breath drawn again (only they move; the words change with
// the breath).
void FocusRest::Frame()
{
    if (done) return;
    double t = Now();
    if (closeAt >= 0) {
        double f = 1 - (t - closeAt) / FadeOut;
        if (f <= 0) {
            done = true;
            KillTimer(Handle(), FrameTimer);
            ShowWindow(Handle(), SW_HIDE);
            PostMessageW(owner, BreakCard::DoneMessage, 1, 0);   // (the owner deletes this)
            return;
        }
        SetLayeredWindowAttributes(Handle(), 0, static_cast<BYTE>(f * 255), LWA_ALPHA);
        return;
    }
    SetLayeredWindowAttributes(Handle(), 0, static_cast<BYTE>(std::min(1.0, t / FadeIn) * 255), LWA_ALPHA);
    if (t - FadeIn >= total) {   // over: the chime, then the screen comes back
        if (sounds) Chime::Play(true);
        Finish();
        return;
    }
    Invalidate(scene->RingBox());
    int cue = CueAt(t);
    if (cue != shownCue) {
        shownCue = cue;
        Invalidate(scene->CueBox());
    }
}

} // namespace bb
