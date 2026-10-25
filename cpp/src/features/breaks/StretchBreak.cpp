#include "features/breaks/StretchBreak.h"
#include "core/Lang.h"
#include "features/reminders/BreakCard.h"
#include "features/reminders/Chime.h"
#include "ui/Theme.h"
#include "ui/controls/Basic.h"
#include <mmsystem.h>
#include <shellscalingapi.h>
#include <algorithm>
#include <cmath>
#include <numbers>

#pragma comment(lib, "winmm.lib")

namespace bb {
namespace {

using namespace ui;
using Pose = StretchBreak::Pose;

constexpr UINT_PTR FrameTimer = 1;
constexpr int Back = Palette::Argb(13, 15, 19);
constexpr float HeadR = 0.12f;
constexpr double Pi = std::numbers::pi;

D2D1_POINT_2F P(double x, double y) { return {static_cast<float>(x), static_cast<float>(y)}; }
D2D1_POINT_2F Mix(D2D1_POINT_2F a, D2D1_POINT_2F b, double k) { return P(a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k); }
double Wave01(double t, double period) { return 0.5 - 0.5 * std::cos(2 * Pi * t / period); }
double Ease(double x) { x = std::clamp(x, 0.0, 1.0); return x * x * (3 - 2 * x); }

// ---- Poses (t: seconds into the stretch) ----

using enum StretchBreak::Joint;

Pose Standing(double t)
{
    double b = 0.007 * std::sin(2 * Pi * t / 4);   // breathing
    Pose p{};
    p[Pelvis] = P(0, 0.95);
    p[Neck] = P(0, 1.45 + b);
    p[Head] = P(0, 1.62 + b);
    p[ShL] = P(-0.19, 1.43 + b); p[ShR] = P(0.19, 1.43 + b);
    p[ElL] = P(-0.23, 1.17 + b); p[ElR] = P(0.23, 1.17 + b);
    p[HaL] = P(-0.25, 0.92 + b); p[HaR] = P(0.25, 0.92 + b);
    p[HipL] = P(-0.1, 0.95); p[HipR] = P(0.1, 0.95);
    p[KnL] = P(-0.11, 0.5); p[KnR] = P(0.11, 0.5);
    p[FtL] = P(-0.13, 0.05); p[FtR] = P(0.13, 0.05);
    p[Toe] = P(-0.2, 0.0); p[ToeR] = P(0.2, 0.0);
    return p;
}

void Lift(Pose& p, double dy, bool feetToo)
{
    for (int i = 0; i < StretchBreak::JointCount; i++)
        if (feetToo || (i != Toe && i != ToeR)) p[i] = P(p[i].x, p[i].y + dy);
}

void Lean(Pose& p, double dx)   // shifts the upper body sideways, more the higher it is
{
    for (int i : {Head, Neck, ShL, ShR, ElL, ElR, HaL, HaR}) p[i] = P(p[i].x + dx * (p[i].y - 0.95) / 0.7, p[i].y);
}

// From sitting to standing, then breathing.
Pose StandUp(double t)
{
    Pose p = Standing(t);
    double up = Ease((t - 0.6) / 1.6), down = (1 - up) * 0.36;
    if (down <= 0) return p;
    for (int i : {Head, Neck, ShL, ShR, ElL, ElR, HaL, HaR, Pelvis, HipL, HipR}) p[i] = P(p[i].x, p[i].y - down);
    // Seated, the thighs point at us: the knees sit just below the hips and a little wider.
    p[KnL] = Mix(p[KnL], P(-0.16, 0.52), 1 - up); p[KnR] = Mix(p[KnR], P(0.16, 0.52), 1 - up);
    p[HaL] = Mix(p[HaL], P(-0.17, 0.58), 1 - up); p[HaR] = Mix(p[HaR], P(0.17, 0.58), 1 - up);   // hands on the knees
    p[ElL] = Mix(p[ElL], P(-0.27, 0.8), 1 - up); p[ElR] = Mix(p[ElR], P(0.27, 0.8), 1 - up);
    return p;
}

Pose Reach(double t)
{
    Pose p = Standing(t);
    double c = Ease(Wave01(t, 6) * 1.25);
    p[ElL] = Mix(p[ElL], P(-0.22, 1.74), c); p[ElR] = Mix(p[ElR], P(0.22, 1.74), c);
    p[HaL] = Mix(p[HaL], P(-0.14, 2.02), c); p[HaR] = Mix(p[HaR], P(0.14, 2.02), c);
    Lean(p, 0.08 * c * std::sin(2 * Pi * t / 12));
    Lift(p, 0.035 * c, false);
    return p;
}

Pose Shoulders(double t)
{
    Pose p = Standing(t);
    double th = 2 * Pi * t / 2.6, dy = 0.04 * std::sin(th), dx = 0.022 * std::cos(th);
    p[ShL] = P(p[ShL].x - dx, p[ShL].y + dy); p[ShR] = P(p[ShR].x + dx, p[ShR].y + dy);
    p[ElL] = P(p[ShL].x - 0.04, p[ShL].y - 0.26); p[ElR] = P(p[ShR].x + 0.04, p[ShR].y - 0.26);
    p[HaL] = P(p[ElL].x - 0.02, p[ElL].y - 0.25); p[HaR] = P(p[ElR].x + 0.02, p[ElR].y - 0.25);
    return p;
}

Pose NeckTilt(double t)
{
    Pose p = Standing(t);
    double a = 0.5 * std::sin(2 * Pi * t / 7);
    p[Head] = P(p[Neck].x + 0.17 * std::sin(a), p[Neck].y + 0.17 * std::cos(a));
    return p;
}

Pose Twist(double t)
{
    Pose p = Standing(t);
    double phi = 0.9 * std::sin(2 * Pi * t / 5), c = std::cos(phi), sn = std::sin(phi);
    p[ShL] = P(-0.19 * c, p[ShL].y); p[ShR] = P(0.19 * c, p[ShR].y);
    // Forearms folded across the chest, turning with it.
    p[HaL] = P(0.04 + 0.16 * sn, 1.3); p[HaR] = P(-0.04 + 0.16 * sn, 1.33);
    p[ElL] = P(p[ShL].x - 0.1 * c + 0.05 * sn, 1.22); p[ElR] = P(p[ShR].x + 0.1 * c + 0.05 * sn, 1.22);
    p[Head] = P(p[Head].x + 0.035 * sn, p[Head].y);
    return p;
}

Pose Chest(double t)
{
    Pose p = Standing(t);
    double open = Ease(Wave01(t, 4.5) * 1.2);
    p[ElL] = Mix(P(-0.24, 1.3), P(-0.4, 1.42), open); p[ElR] = Mix(P(0.24, 1.3), P(0.4, 1.42), open);
    p[HaL] = Mix(P(-0.1, 1.38), P(-0.43, 1.7), open); p[HaR] = Mix(P(0.1, 1.38), P(0.43, 1.7), open);
    p[ShL] = P(p[ShL].x - 0.02 * open, p[ShL].y); p[ShR] = P(p[ShR].x + 0.02 * open, p[ShR].y);
    return p;
}

Pose March(double t)
{
    Pose p = Standing(t);
    double s = std::sin(2 * Pi * t / 1.5), l = std::max(0.0, s), r = std::max(0.0, -s);
    p[KnL] = P(-0.12, 0.5 + 0.26 * l); p[FtL] = P(-0.12, 0.05 + 0.3 * l); p[Toe] = P(-0.19, 0.0 + 0.3 * l);
    p[KnR] = P(0.12, 0.5 + 0.26 * r); p[FtR] = P(0.12, 0.05 + 0.3 * r); p[ToeR] = P(0.19, 0.0 + 0.3 * r);
    // Arms swing with the opposite leg.
    p[ElR] = P(0.24, 1.17 + 0.05 * l); p[HaR] = P(0.22, 0.92 + 0.2 * l);
    p[ElL] = P(-0.24, 1.17 + 0.05 * r); p[HaL] = P(-0.22, 0.92 + 0.2 * r);
    Lift(p, 0.015 * std::abs(s), true);
    return p;
}

Pose Calves(double t)
{
    Pose p = Standing(t);
    double up = Ease(Wave01(t, 3.2) * 1.2);
    p[ElL] = P(-0.31, 1.12); p[ElR] = P(0.31, 1.12);   // hands on hips
    p[HaL] = P(-0.14, 0.99); p[HaR] = P(0.14, 0.99);
    Lift(p, 0.075 * up, false);   // the toes stay down, the heels rise
    return p;
}

// Seen from the side: hinge forward at the hips, arms hanging, knees soft.
Pose Fold(double t)
{
    double bend = Ease(Wave01(t, 7) * 1.15), a = bend * 75 * Pi / 180;
    Pose p{};
    D2D1_POINT_2F pelvis = P(-0.06 * bend, 0.95 - 0.03 * bend);
    p[Pelvis] = pelvis; p[HipL] = pelvis; p[HipR] = pelvis;
    p[Neck] = P(pelvis.x + 0.5 * std::sin(a), pelvis.y + 0.5 * std::cos(a));
    p[Head] = P(p[Neck].x + 0.17 * std::sin(a + 0.15 * bend), p[Neck].y + 0.17 * std::cos(a + 0.15 * bend));
    p[ShL] = P(p[Neck].x - 0.02, p[Neck].y - 0.02); p[ShR] = p[ShL];
    p[ElL] = P(p[ShL].x + 0.02, p[ShL].y - 0.27); p[ElR] = P(p[ShL].x + 0.05, p[ShL].y - 0.27);
    p[HaL] = P(p[ElL].x + 0.03, p[ElL].y - 0.26); p[HaR] = P(p[ElR].x + 0.03, p[ElR].y - 0.26);
    p[KnL] = P(0.05 + 0.04 * bend, 0.5); p[KnR] = P(0.08 + 0.04 * bend, 0.5);
    p[FtL] = P(-0.02, 0.05); p[FtR] = P(0.01, 0.05);
    p[Toe] = P(0.1, 0.0); p[ToeR] = P(0.13, 0.0);
    return p;
}

Pose Wave(double t)
{
    Pose p = Standing(t);
    double w = std::sin(2 * Pi * t / 0.9);
    p[ElR] = P(0.36, 1.45); p[HaR] = P(0.42 + 0.06 * w, 1.75);
    return p;
}

const StretchBreak::Stretch Routine[] = {
    {L"stretch_reach", L"Overhead reach", L"Reach both arms up high, and stretch tall.", Reach},
    {L"stretch_shoulders", L"Shoulder rolls", L"Roll your shoulders slowly backwards.", Shoulders},
    {L"stretch_neck", L"Neck stretch", L"Gently tilt your head toward each shoulder.", NeckTilt},
    {L"stretch_twist", L"Torso twist", L"Twist your upper body slowly, side to side.", Twist},
    {L"stretch_chest", L"Chest opener", L"Open your arms wide, and squeeze your shoulder blades together.", Chest},
    {L"stretch_march", L"March in place", L"March in place, lifting your knees.", March},
    {L"stretch_calves", L"Calf raises", L"Rise up onto your toes, then lower slowly.", Calves},
    {L"stretch_fold", L"Forward bend", L"Bend your knees a little, and hang forward to stretch your back.", Fold},
};
const StretchBreak::Stretch Intro{L"stretch_intro", L"Time to stand up", L"Time to stand up. Follow along with a few easy stretches.", StandUp};
const StretchBreak::Stretch Outro{L"stretch_done", L"Great job!", L"Great job. Have a sip of water, and back to it.", Wave};

// Morphs from the last pose into the new stretch over the first moments of it.
Pose Blend(const Pose* a, const Pose& b, double since)
{
    if (!a) return b;
    double k = Ease(std::clamp(since / 0.9, 0.0, 1.0));
    if (k >= 1) return b;
    Pose p{};
    for (int i = 0; i < StretchBreak::JointCount; i++) p[i] = Mix((*a)[i], b[i], k);
    return p;
}

} // namespace

// The whole screen: the figure on its glowing shadow, the stretch's name and words, and the progress at the bottom.
class StretchBreak::Coach : public Control {
public:
    explicit Coach(StretchBreak& owner) : owner(owner) {}

    float K() const { return H() * 0.44f / 2.05f; }   // body units to 96-dpi units
    D2D1_POINT_2F Floor() const { return {W() / 2, H() * 0.66f}; }
    D2D1_POINT_2F ToScreen(D2D1_POINT_2F b) const { return {Floor().x + b.x * K(), Floor().y - b.y * K()}; }
    D2D1_RECT_F FigureBox() const
    {
        float k = K();
        return {Floor().x - 0.75f * k, Floor().y - 2.25f * k, Floor().x + 0.75f * k, Floor().y + 0.15f * k};
    }
    D2D1_RECT_F FooterBox() const { return {0, H() - 120, W() - 300, H()}; }

    void Paint(Canvas& c) override
    {
        DrawShadow(c);
        DrawFigure(c);
        DrawTitles(c);
        DrawFooter(c);
    }

private:
    void DrawShadow(Canvas& c)
    {
        float k = K(), w = 0.8f * k, h = 0.1f * k;
        D2D1_GRADIENT_STOP stops[] = {{0, gfx::Color(Theme::Accent(), 70 / 255.0f)}, {1, gfx::Color(Theme::Accent(), 0)}};
        gfx::ComPtr<ID2D1GradientStopCollection> collection;
        gfx::ComPtr<ID2D1RadialGradientBrush> glow;
        c.Target()->CreateGradientStopCollection(stops, 2, collection.GetAddressOf());
        c.Target()->CreateRadialGradientBrush({Floor(), {0, 0}, w / 2, h / 2}, collection.Get(), glow.GetAddressOf());
        c.Target()->FillEllipse({Floor(), w / 2, h / 2}, glow.Get());
    }

    void DrawFigure(Canvas& c)
    {
        const Pose& p = owner.pose;
        float k = K(), w = std::max(3.0f, k * 0.05f);
        static const std::vector<std::vector<int>> limbs = {
            {Neck, Pelvis}, {ShL, ShR}, {HipL, HipR}, {ShL, ElL, HaL}, {ShR, ElR, HaR}, {HipL, KnL, FtL, Toe}, {HipR, KnR, FtR, ToeR}};
        gfx::ComPtr<ID2D1StrokeStyle> round;
        D2D1_STROKE_STYLE_PROPERTIES props{};
        props.startCap = props.endCap = D2D1_CAP_STYLE_ROUND;
        props.lineJoin = D2D1_LINE_JOIN_ROUND;
        gfx::D2D()->CreateStrokeStyle(props, nullptr, 0, round.GetAddressOf());
        // A soft glow underneath, then the limbs.
        struct Pass { float width; int color; float alpha; };
        for (Pass pass : {Pass{w * 3.2f, Theme::Accent(), 38 / 255.0f}, Pass{w, Palette::Argb(232, 238, 246), 1}}) {
            ID2D1SolidColorBrush* brush = c.Brush(pass.color, pass.alpha);
            for (const auto& limb : limbs) {
                gfx::ComPtr<ID2D1PathGeometry> path;
                gfx::ComPtr<ID2D1GeometrySink> sink;
                gfx::D2D()->CreatePathGeometry(path.GetAddressOf());
                path->Open(sink.GetAddressOf());
                sink->BeginFigure(ToScreen(p[limb[0]]), D2D1_FIGURE_BEGIN_HOLLOW);
                for (size_t i = 1; i < limb.size(); i++) sink->AddLine(ToScreen(p[limb[i]]));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                sink->Close();
                c.Target()->DrawGeometry(path.Get(), brush, pass.width, round.Get());
            }
            D2D1_POINT_2F neck = ToScreen(p[Neck]), head = ToScreen(p[Head]);
            c.Target()->DrawLine(neck, Mix(neck, head, 0.4), brush, pass.width, round.Get());
            float r = HeadR * k;
            c.FillCircle(head.x, head.y, r, Back);
            c.Target()->DrawEllipse({head, r, r}, c.Brush(pass.color, pass.alpha), pass.width);
        }
        // Hands as small dots, for a little life.
        for (int j : {HaL, HaR}) {
            D2D1_POINT_2F h = ToScreen(p[j]);
            c.FillCircle(h.x, h.y, w * 0.9f, Theme::Accent());
        }
    }

    void DrawTitles(Canvas& c)
    {
        const Stretch& now = owner.current >= static_cast<int>(owner.plan.size()) ? Outro : owner.current < 0 ? Intro : owner.plan[owner.current];
        float y = Floor().y + K() * 0.25f, titleH = 30 * 96 / 72.0f * 1.35f, textH = 13 * 96 / 72.0f * 1.35f;
        c.Text(Lang::T(now.title), Fonts::Get(30, gfx::Weight::Light), {0, y, W(), y + titleH * 1.3f}, Palette::Argb(232, 236, 242), HAlign::Center);
        c.Text(Lang::T(now.text), Fonts::Get(13), {W() * 0.2f, y + titleH * 1.25f, W() * 0.8f, y + titleH * 1.25f + textH * 2.4f},
               Palette::Argb(138, 145, 156), HAlign::Center, true, true);
    }

    void DrawFooter(Canvas& c)
    {
        float m = 28, barY = H() - m - 17, barW = W() * 0.32f;
        double end = owner.starts.back() + owner.outroLen, t = owner.Now();
        float progress = static_cast<float>(std::clamp(t / end, 0.0, 1.0));
        c.Line(m, barY, m + barW, barY, Palette::Argb(48, 52, 60), 4);
        if (progress > 0) c.Line(m, barY, m + barW * progress, barY, Theme::Accent(), 4);
        int left = std::max(0, owner.shownSeconds);
        std::wstring next;
        if (owner.current >= 0 && owner.current + 1 < static_cast<int>(owner.plan.size()))
            next = Lang::F(L"Next: {0}", {Lang::T(owner.plan[owner.current + 1].title)}) + L"  ·  ";
        wchar_t ss[4];
        swprintf_s(ss, L"%02d", left % 60);
        std::wstring time = owner.forced ? std::to_wstring(left / 60) + L":" + ss
                                         : Lang::F(L"{0}:{1:00} left  ·  Esc to skip", {std::to_wstring(left / 60), ss});
        float smallH = 10 * 96 / 72.0f * 1.35f;
        c.Text(next + time, Fonts::Get(10), {m, barY - smallH * 1.8f, W(), barY - smallH * 0.4f}, Palette::Argb(110, 116, 126));
    }

    StretchBreak& owner;
};

StretchBreak::StretchBreak(HWND owner, const Settings& s) : owner(owner), sounds(s.Sounds), forced(s.ForceRest)
{
    if (s.StretchVoice) voice = std::make_unique<Voice>();
    double total = s.LongBreakDuration * 60.0;
    lead = sounds ? 1.0 : 0.3;
    double introLen = std::max(4.0, Voice::Length(Intro.key) + Voice::Pause + 1.0);
    outroLen = std::max(3.0, Voice::Length(Outro.key) + 0.8);
    int count = std::clamp(static_cast<int>(std::lround(total / 38)), 2, 16);
    double seg = std::max(15.0, (total - lead - introLen - outroLen) / count);
    plan.push_back(Intro);
    starts.push_back(0);
    double t = lead + introLen;
    for (int i = 0; i < count; i++) {
        plan.push_back(Routine[i % std::size(Routine)]);
        starts.push_back(t);
        t += seg;
    }
    starts.push_back(t);   // where the last one ends

    coach = &Root().Add<Coach>(*this);
    skip = &Root().Add<Button>(L"Skip");
    skip->onClick = [this] { Finish(0); };
    later = &Root().Add<Button>(L"Snooze 5 min");
    later->onClick = [this] { Finish(300); };
    skip->SetVisible(!forced);
    later->SetVisible(!forced);
    pose = StandUp(0);

    // The whole screen under the pointer, faded in.
    POINT cursor;
    GetCursorPos(&cursor);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &mi);
    Options opts;
    opts.popup = true;
    opts.edge = false;
    opts.background = Back;
    float sc = 1;
    {
        UINT dx = 96, dy = 96;
        GetDpiForMonitor(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &dx, &dy);
        sc = dx / 96.0f;
    }
    POINT at{mi.rcMonitor.left, mi.rcMonitor.top};
    Open((mi.rcMonitor.right - mi.rcMonitor.left) / sc, (mi.rcMonitor.bottom - mi.rcMonitor.top) / sc, &at, opts);
    SetWindowLongPtrW(Handle(), GWL_EXSTYLE, GetWindowLongPtrW(Handle(), GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TOPMOST);
    SetLayeredWindowAttributes(Handle(), 0, 0, LWA_ALPHA);
    SetWindowPos(Handle(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    Show(true);
    timeBeginPeriod(1);
    if (sounds) Chime::Play(false);
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&clockStart);
    SetTimer(Handle(), FrameTimer, 15, nullptr);
}

StretchBreak::~StretchBreak()
{
    if (Handle()) KillTimer(Handle(), FrameTimer);
    timeEndPeriod(1);
}

void StretchBreak::Layout()
{
    D2D1_SIZE_F size = ClientSize();
    if (coach) coach->SetBounds(0, 0, size.width, size.height);
    float h = 34, m = 28;
    if (skip) skip->SetBounds(size.width - m - 88, size.height - m - h, 88, h);
    if (later) later->SetBounds(size.width - m - 88 - 140, size.height - m - h, 128, h);
}

double StretchBreak::Now() const
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart - clockStart.QuadPart) / frequency.QuadPart;
}

bool StretchBreak::Shortcut(UINT key)
{
    if (key != VK_ESCAPE || forced) return false;
    Finish(0);
    return true;
}

LRESULT StretchBreak::OnMessage(UINT message, WPARAM w, LPARAM l)
{
    if (message == WM_TIMER && w == FrameTimer) {
        Frame();
        return 0;
    }
    return DirectWindow::OnMessage(message, w, l);
}

void StretchBreak::Finish(int seconds)
{
    if (closeAt >= 0) return;
    snooze = seconds;
    if (voice) voice->Stop();
    closeAt = Now();
}

void StretchBreak::Frame()
{
    if (done) return;
    double t = Now();
    if (closeAt >= 0) {
        double f = 1 - (t - closeAt) / 0.6;
        if (f <= 0) {
            done = true;
            KillTimer(Handle(), FrameTimer);
            ShowWindow(Handle(), SW_HIDE);
            PostMessageW(owner, BreakCard::DoneMessage, 1, snooze);   // (the owner deletes this)
            return;
        }
        SetLayeredWindowAttributes(Handle(), 0, static_cast<BYTE>(f * 255), LWA_ALPHA);
        return;
    }
    SetLayeredWindowAttributes(Handle(), 0, static_cast<BYTE>(std::min(1.0, t / 0.5) * 255), LWA_ALPHA);
    D2D1_RECT_F before = coach->FigureBox();
    double end = starts.back();
    if (t >= end) {
        // The goodbye: a wave and the closing line, played to its end, then the chime.
        if (outroAt < 0) {
            outroAt = t;
            from = pose;
            hasFrom = true;
            blendStart = t;
            if (voice) voice->Say(Outro.key, Lang::T(Outro.text));
            current = static_cast<int>(plan.size());
            InvalidateAll();
        }
        pose = Blend(hasFrom ? &from : nullptr, Outro.pose(t - outroAt), t - blendStart);
        if (t - outroAt >= outroLen) {
            if (sounds) Chime::Play(true);
            Finish(0);
        }
        Invalidate(before);
        return;
    }
    int now = 0;
    while (now + 1 < static_cast<int>(plan.size()) && t >= starts[now + 1]) now++;
    double u = t - starts[now];
    if (now != current) {
        current = now;
        from = pose;
        hasFrom = true;
        blendStart = t;
        InvalidateAll();   // the titles change
    }
    if (voice && spoken != now && (now > 0 || t >= lead)) {
        spoken = now;
        voice->Say(plan[now].key, Lang::T(plan[now].text));
    }
    int left = static_cast<int>(std::ceil(end + outroLen - t));
    if (left != shownSeconds) {
        shownSeconds = left;
        Invalidate(coach->FooterBox());
    }
    pose = Blend(hasFrom ? &from : nullptr, plan[now].pose(now == 0 ? std::max(0.0, t - lead) : u), t - blendStart);
    Invalidate(before);   // where the figure is (it stays within this box)
}

} // namespace bb
