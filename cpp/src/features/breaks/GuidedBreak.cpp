#include "features/breaks/GuidedBreak.h"
#include "core/Lang.h"
#include "features/reminders/BreakCard.h"
#include "features/reminders/Chime.h"
#include "platform/Activity.h"
#include "ui/Access.h"
#include "ui/DirectWindow.h"
#include "ui/controls/Basic.h"
#include <mmsystem.h>
#include <algorithm>
#include <cmath>
#include <numbers>

#pragma comment(lib, "winmm.lib")

namespace bb {
namespace {

constexpr UINT_PTR FrameTimer = 1;
constexpr double Pi = std::numbers::pi;

struct StepText { const wchar_t* key; const wchar_t* text; };
constexpr StepText Steps[] = {
    {L"far", L"Look away from the screen, at something far away, and let your eyes soften."},
    {L"sides", L"Slowly look left and right, keeping your head still."},
    {L"updown", L"Now slowly look up and down."},
    {L"circle", L"Roll your eyes in a slow, wide circle."},
    {L"close", L"Close your eyes gently, then blink slowly a few times."},
};
constexpr int StepCount = 5;
const std::vector<double> StepWeight{0.5, 1, 1, 1, 1};   // looking far away needs less time than the rest
constexpr wchar_t DoneKey[] = L"done", DoneText[] = L"Well done. You can look back now.";
constexpr double FadeIn = 0.5, FadeOut = 0.9;
// The tail: three smaller blobs, each on a soft spring behind the one before.
constexpr double TailR[] = {0.68, 0.52, 0.38}, TailM[] = {0.95, 0.85, 0.75}, TailK[] = {520, 430, 350};
// The snail trail: little blobs dropped along the way, drying up over TrailLife seconds.
constexpr int TrailMax = 240, Body = 1 + 3;
constexpr double TrailLife = 1.1;

double Smooth(double x) { x = std::clamp(x, 0.0, 1.0); return x * x * (3 - 2 * x); }
double Smoother(double x) { x = std::clamp(x, 0.0, 1.0); return x * x * x * (x * (6 * x - 15) + 10); }
double Pop(double x)   // ease out with an overshoot: liquid settling
{
    x = std::clamp(x, 0.0, 1.0);
    constexpr double c = 1.9;
    return 1 + (c + 1) * std::pow(x - 1, 3) + c * std::pow(x - 1, 2);
}
// A wobble dying away: how hard it is still squashed, ti seconds after a hit.
double Jiggle(double ti, double hold, double hit) { return hold + hit * std::exp(-ti * 6) * std::cos(ti * 21); }
// Even speed with eased ends: a fraction ramp of the time to get going, the same to stop.
double Cruise(double x, double ramp)
{
    x = std::clamp(x, 0.0, 1.0);
    double k = 1 / (1 - ramp);
    if (x < ramp) return k * x * x / (2 * ramp);
    if (x > 1 - ramp) return 1 - k * (1 - x) * (1 - x) / (2 * ramp);
    return k * (x - ramp / 2);
}
int EdgeOf(bool sideways, int side) { return sideways ? (side < 0 ? 0 : 1) : (side < 0 ? 2 : 3); }

std::vector<std::wstring> Keys()
{
    std::vector<std::wstring> keys;
    for (const auto& s : Steps) keys.push_back(s.key);
    return keys;
}

} // namespace

// A window above (or beside) the break: a dark shade over another screen, or a click-through see-through layer for
// the liquid or the words.
class GuidedBreak::Layer : public Window {
public:
    Layer(const RECT& r, HWND owner, bool solidBlack)
    {
        Create(L"BlinkBuddy.BreakLayer", WS_POPUP, PopupExStyle | WS_EX_LAYERED | WS_EX_TRANSPARENT, r,
               solidBlack ? static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)) : nullptr, owner);
        if (solidBlack) SetLayeredWindowAttributes(hwnd, 0, 3, LWA_ALPHA);
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    }
    void SetOpacity(double k) { SetLayeredWindowAttributes(hwnd, 0, static_cast<BYTE>(std::clamp(k, 0.012, 1.0) * 255), LWA_ALPHA); }
};

// Skip and Snooze at the bottom right, as on the stand-up break: a small window of its own above the break (clicks
// on it don't fall through to the break), fading in and out with it. Esc works here too once it has been clicked.
class GuidedBreak::Buttons : public ui::DirectWindow {
public:
    Buttons(GuidedBreak& owner, const RECT& monitor, float scale, bool backdrop) : owner(owner)
    {
        auto& later = Root().Add<ui::Button>(L"Snooze 5 min");
        later.SetBounds(0, 0, 128, 34);
        later.onClick = [this] { this->owner.End(300); };
        auto& skip = Root().Add<ui::Button>(L"Skip");
        skip.SetBounds(140, 0, 88, 34);
        skip.onClick = [this] { this->owner.End(0); };
        Options opts;
        opts.popup = true;
        opts.edge = false;
        opts.background = backdrop ? Palette::Argb(0, 0, 0) : Palette::Argb(12, 14, 18);
        opts.owner = owner.Handle();
        POINT at{monitor.right - static_cast<LONG>((28 + 228) * scale), monitor.bottom - static_cast<LONG>((28 + 34) * scale)};
        Open(228, 34, &at, opts);
        SetWindowLongPtrW(Handle(), GWL_EXSTYLE, GetWindowLongPtrW(Handle(), GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TOPMOST);
        SetOpacity(0);
        Show(false);
    }
    void SetOpacity(double k) { SetLayeredWindowAttributes(Handle(), 0, static_cast<BYTE>(std::clamp(k, 0.0, 1.0) * 255), LWA_ALPHA); }

protected:
    bool Shortcut(UINT key) override
    {
        if (key != VK_ESCAPE) return false;
        owner.End(0);
        return true;
    }

private:
    GuidedBreak& owner;
};

GuidedBreak::GuidedBreak(HWND owner, const Settings& s, float warmth)
    : owner(owner), sounds(s.Sounds), forced(s.ForceRest), backdrop(s.BreakBackdrop), liquid(Body + TrailMax), trailBorn(TrailMax, -100)
{
    liquid.warm = warmth;
    std::vector<std::wstring> keys = Keys();
    int needed = Voice::MinimumSeconds(Lang::Code(), Voice::LookAwayKeys());
    // Voice only when every line fits; otherwise a line would have to be cut.
    bool voiced = s.ExerciseVoice && (needed == 0 || s.BreakDuration >= needed);
    if (voiced) voice = std::make_unique<Voice>(Lang::Code());
    plan.emplace(keys, DoneKey, s.BreakDuration, 2.5, sounds ? 1.0 : 0.3, voiced && needed > 0, StepWeight,
                 [](const std::wstring& k) { return Voice::Length(Lang::Code(), k); }, Voice::Pause);

    POINT cursor;
    GetCursorPos(&cursor);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &mi);
    bounds = mi.rcMonitor;
    width = bounds.right - bounds.left;
    height = bounds.bottom - bounds.top;
    scale = ScaleAt(cursor);
    captionFont = gfx::Font(14);
    hintFont = gfx::Font(10);

    // The background: black (or almost nothing when the break plays over the screen; it still takes the keyboard for Esc).
    Create(L"BlinkBuddy.GuidedBreak", WS_POPUP, WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, bounds,
           static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    if (backdrop) {
        EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR, HDC, LPRECT r, LPARAM p) {
            auto self = reinterpret_cast<GuidedBreak*>(p);
            if (!EqualRect(r, &self->bounds)) self->shades.push_back(std::make_unique<Layer>(*r, nullptr, true));
            return TRUE;
        }, reinterpret_cast<LPARAM>(this));
    }
    // The liquid and the words float above, in windows of their own (owned, so always above this one).
    drop = std::make_unique<Layer>(bounds, hwnd, false);
    caption = std::make_unique<Layer>(bounds, hwnd, false);
    if (!forced) buttons = std::make_unique<Buttons>(*this, bounds, scale, backdrop);
    timeBeginPeriod(1);   // even frame times for the length of the break
    if (sounds) Chime::Play(false);
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&clockStart);
    SetTimer(hwnd, FrameTimer, 15, nullptr);
}

GuidedBreak::~GuidedBreak()
{
    if (hwnd) KillTimer(hwnd, FrameTimer);
    timeEndPeriod(1);
    buttons.reset();
    drop.reset();
    caption.reset();
    shades.clear();
    Destroy();
}

double GuidedBreak::Now() const
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart - clockStart.QuadPart) / frequency.QuadPart;
}

LRESULT GuidedBreak::OnMessage(UINT message, WPARAM w, LPARAM l)
{
    switch (message) {
    case WM_TIMER:
        if (w == FrameTimer) Frame();
        return 0;
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) End(0);
        return 0;
    }
    return Window::OnMessage(message, w, l);
}

void GuidedBreak::End(int snoozeSeconds)
{
    if (fadeAt >= 0 || forced) return;
    snooze = snoozeSeconds;
    if (voice) voice->Stop();
    fadeAt = Now();
}

void GuidedBreak::Frame()
{
    if (finished) return;
    double t = Now(), dt = std::clamp(t - lastT, 0.001, 0.05);
    lastT = t;
    if (fadeAt >= 0) {
        double f = 1 - (t - fadeAt) / FadeOut;
        if (f <= 0) {
            finished = true;
            KillTimer(hwnd, FrameTimer);
            ShowWindow(hwnd, SW_HIDE);
            if (buttons) ShowWindow(buttons->Handle(), SW_HIDE);
            PostMessageW(owner, BreakCard::DoneMessage, 0, snooze);   // (the owner deletes this)
            return;
        }
        SetFade(f);
        return;
    }
    SetFade(std::min(1.0, t / FadeIn));
    double run = t - FadeIn;   // time into the exercises

    // After the last step: the drop fades away, the closing line plays to its end, then the chime, then the screen
    // comes back.
    if (run >= plan->end) {
        if (outroAt < 0) {
            outroAt = t;
            if (voice) voice->Say(DoneKey, Lang::T(DoneText));
            UpdateCaption();
        }
        Animate(StepCount, t - outroAt, 1, dt, t);
        Redraw();
        double wait = voice ? std::max(0.9, Voice::Length(Lang::Code(), DoneKey) + 0.25) : 0.9;
        if (t - outroAt >= wait) {
            if (sounds) Chime::Play(true);
            fadeAt = t;
        }
        return;
    }

    double u;
    int now = plan->At(run, u);
    if (now != step) {
        step = now;
        UpdateCaption();
        if (!voice && step >= 0) ui::Access::Announce(hwnd, Lang::T(Steps[step].text));   // (spoken anyway with the voice)
    }
    if (voice && step >= 0 && spoken != step && (step > 0 || u >= plan->lead)) {
        spoken = step;
        voice->Say(Steps[step].key, Lang::T(Steps[step].text));
    }
    int left = static_cast<int>(std::ceil(plan->end - std::max(0.0, run)));
    if (left != shownSeconds) {
        shownSeconds = left;
        UpdateCaption();
    }
    Animate(step, u, step >= 0 ? plan->length[step] : 1, dt, t);
    Redraw();
}

// The fade in and out: the background, the other screens' shades, and the liquid and words above.
void GuidedBreak::SetFade(double f)
{
    double eased = f * f * (3 - 2 * f);
    if (std::abs(eased - fadeK) < 0.001 && fadeK > 0) return;
    fadeK = eased;
    SetLayeredWindowAttributes(hwnd, 0, static_cast<BYTE>(std::clamp(backdrop ? eased : 0.012, 0.012, 1.0) * 255), LWA_ALPHA);
    for (auto& sh : shades) sh->SetOpacity(std::min(0.95, eased));
    if (buttons) buttons->SetOpacity(eased);
    ShowCaption();
    if (liquidShown) liquid.Present(drop->Handle(), {bounds.left, bounds.top}, static_cast<BYTE>(255 * fadeK));
}

// ---- Timing ----

// How an exercise spends its step: a glance (and any setting off), the movement repeated, the way back to the
// middle, then a rest until the next exercise. A movement takes up to 3 s; with more time it is done twice, then
// twice with a rest of up to 4 s after it, then three or more times.
GuidedBreak::Timing GuidedBreak::Plan(double len, double pre, double back)
{
    Timing tm{pre, 0, 0, back, 1};
    double a = std::max(0.8, len - pre - back);
    if (a <= 3) {
        tm.reps = 1;
        tm.cycle = a;
    } else if (a <= 6) {
        tm.reps = 2;
        tm.cycle = a / 2;
    } else if (a <= 10) {
        tm.reps = 2;
        tm.cycle = 3;
    } else {
        tm.reps = std::max(3, static_cast<int>(std::ceil((a - 4) / 3)));
        tm.cycle = (a - 4) / tm.reps;
    }
    tm.moves = tm.reps * tm.cycle;
    return tm;
}

// ---- Motion ----

void GuidedBreak::Animate(int k, double u, double len, double dt, double t)
{
    // By default: resting in the middle, looking at you, breathing.
    px = CX();
    py = CY();
    grow = 1 + 0.022 * std::sin(t * 2.1);
    mainM = flatX = flatY = tail = stiff = open = 1;
    stick = eyeShape = lookTX = lookTY = 0;
    axis = -1;
    alpha = 1;
    trailOn = false;
    for (auto& f : liquid.films) f.a = 0;
    if (k < 0) {
        alpha = 0;
        return;
    }
    if (k == 0) Appear(u);
    else if (k == 1) Sides(u, len);
    else if (k == 2) UpDown(u, len);
    else if (k == 3) Circle(u, len);
    else if (k == 4) Blink(u, len);
    else {
        double g = Smooth((u - 0.1) / 0.7);   // the end: it shrinks away
        alpha = static_cast<float>(1 - g);
        grow *= 1 - 0.5 * g;
    }

    Liquid::Blob& eye = liquid.blobs[0];
    double R = Radius();
    if (!started) {
        started = true;
        prevX = trailX = px;
        prevY = trailY = py;
        for (int j = 1; j < Body; j++) {
            liquid.blobs[j].x = px;
            liquid.blobs[j].y = py;
        }
    }

    // Viscosity: the stretch chases the speed through a soft spring, so it lags, overshoots and wobbles.
    double vx = (px - prevX) / dt, vy = (py - prevY) / dt, speed = std::sqrt(vx * vx + vy * vy);
    double target = 1 + std::min(0.6, speed / (std::max(width, height) * 0.7));
    stretchSpeed += (170 * (target - stretch) - 9 * stretchSpeed) * dt;
    stretch += stretchSpeed * dt;
    double along = stretch, across = 1 / std::sqrt(stretch), sx = 1, sy = 1;
    if (axis == 0) {
        sx = along;
        sy = across;
    } else if (axis == 1) {
        sx = across;
        sy = along;
    } else if (axis == 2 && speed > 1) {
        double c2 = vx * vx / (speed * speed);
        sx = across + (along - across) * c2;
        sy = along + (across - along) * c2;
    }
    eye.r = R * grow;
    eye.m = mainM;
    eye.sx = sx * flatX * (1 + 0.22 * eyeShape);
    eye.sy = sy * flatY * (1 - 0.14 * eyeShape);

    // The tail: each blob springs towards the one ahead of it (or, while still stuck, towards the wall).
    int n = std::max(1, static_cast<int>(std::ceil(dt * 240)));
    double h = dt / n;
    for (int s = 1; s <= n; s++) {
        eye.x = prevX + (px - prevX) * s / n;
        eye.y = prevY + (py - prevY) * s / n;
        for (int j = 1; j < Body; j++) {
            Liquid::Blob& b = liquid.blobs[j];
            const Liquid::Blob& ahead = liquid.blobs[j - 1];
            double hold = std::clamp(stick * (0.6 + 0.4 * j / 3.0), 0.0, 1.0);
            double gx = ahead.x + (anchorX - ahead.x) * hold, gy = ahead.y + (anchorY - ahead.y) * hold;
            double kk = TailK[j - 1] * stiff, damp = 2 * std::sqrt(kk) * 0.4;
            b.vx += (kk * (gx - b.x) - damp * b.vx) * h;
            b.vy += (kk * (gy - b.y) - damp * b.vy) * h;
            b.x += b.vx * h;
            b.y += b.vy * h;
            // Surface tension: a blob never gets further from the one ahead than the liquid can stretch (much further
            // while it is stuck to a wall, which is the sticky neck), so the tail stays one piece.
            double ex = b.x - ahead.x, ey = b.y - ahead.y, d = std::sqrt(ex * ex + ey * ey), most = R * (0.8 + 2.6 * stick);
            if (d > most) {
                b.x = ahead.x + ex * most / d;
                b.y = ahead.y + ey * most / d;
            }
        }
    }
    for (int j = 1; j < Body; j++) {
        liquid.blobs[j].r = R * TailR[j - 1] * grow;
        liquid.blobs[j].m = TailM[j - 1] * mainM * tail * (1 - eyeShape);
    }

    // The eyes swing to where they look, a little past it, and settle: quick, but alive.
    lookVX += (300 * (lookTX - lookX) - 16 * lookVX) * dt;
    lookVY += (300 * (lookTY - lookY) - 16 * lookVY) * dt;
    lookX += lookVX * dt;
    lookY += lookVY * dt;
    liquid.lookX = lookX;
    liquid.lookY = lookY;
    liquid.eyeShape = eyeShape;
    liquid.open = open;
    Trail(t, R);
    prevX = px;
    prevY = py;
}

// The snail trail: while it is on, a small blob is left every little way; each dries up over its life, thinning until
// the line beads up into droplets and is gone.
void GuidedBreak::Trail(double t, double R)
{
    double gap = R * 0.13;
    if (trailOn) {
        double dx = px - trailX, dy = py - trailY, d = std::sqrt(dx * dx + dy * dy);
        for (double s = gap; s <= d; s += gap) {
            trailBorn[trailNext] = t;
            liquid.blobs[Body + trailNext].x = trailX + dx * s / d;
            liquid.blobs[Body + trailNext].y = trailY + dy * s / d;
            trailNext = (trailNext + 1) % TrailMax;
            if (s + gap > d) {
                trailX += dx * s / d;
                trailY += dy * s / d;
            }
        }
    } else {
        trailX = px;
        trailY = py;
    }
    for (int i = 0; i < TrailMax; i++) {
        Liquid::Blob& b = liquid.blobs[Body + i];
        double life = 1 - (t - trailBorn[i]) / TrailLife;
        if (life <= 0) {
            b.m = 0;
            continue;
        }
        b.m = 0.85 * std::sqrt(life) * alpha;
        b.r = R * 0.17 * (0.5 + 0.5 * life);
    }
}

// The first time: it fades in, small and growing, and looks at you.
void GuidedBreak::Appear(double u)
{
    alpha = static_cast<float>(Smooth((u - 0.05) / 1.5));
    grow *= 0.35 + 0.65 * Pop((u - 0.05) / 1.3);
}

// Looking the way it is about to go, leaning that way a touch.
void GuidedBreak::Glance(double u, double lx, double ly)
{
    if (u < 0.06) return;
    lookTX = lx;
    lookTY = ly;
    double lean = Radius() * 0.12 * Smooth((u - 0.06) / 0.3);
    px += lx * lean;
    py += ly * lean;
}

void GuidedBreak::SetFilm(Liquid::Film& f, int edge, double at, double amount, double spread)
{
    f.edge = edge;
    f.at = at;
    f.a = std::clamp(amount, 0.0, 1.0);
    f.spread = spread;
    f.thick = Radius() * 0.95;
}

// Left and right: a glance to the left, then side to side (a movement is there and back), then back home.
void GuidedBreak::Sides(double u, double len)
{
    Timing tm = Plan(len, 0.45, 0.8);
    if (u < tm.pre) {
        Glance(u, -1, 0);
        return;
    }
    double s = u - tm.pre, leg = tm.cycle / 2;
    if (s < tm.moves) {
        int i = std::min(2 * tm.reps - 1, static_cast<int>(s / leg));
        WallLeg(i == 0, i % 2 == 0 ? -1 : 1, (s - i * leg) / leg, leg);
        return;
    }
    s -= tm.moves;
    if (s < tm.back) BackFromSide(1, s / tm.back, tm.back);
}

// One crossing to the wall at side dir: pull free of the wall it is stuck to (or, the first time, draw back to gather
// itself), shoot across, get grabbed by the far wall and slap into it, stuck there with a wobble.
void GuidedBreak::WallLeg(bool fromMiddle, int dir, double v, double dur)
{
    double R = Radius(), W = width, from = dir < 0 ? W : 0, to = dir < 0 ? 0 : W;
    double stuckFrom = from + dir * R * 0.8, stuckTo = to - dir * R * 0.8, approach = to - dir * R * 2.5;
    constexpr double P1 = 0.3, P2 = 0.6, P3 = 0.68;
    double start = fromMiddle ? CX() - dir * R * 0.45 : from + dir * R * 2.1, p, sq = 0;
    axis = 0;
    lookTX = dir;
    if (v < P1) {
        double x = v / P1;
        if (fromMiddle) {
            p = CX() - dir * R * 0.45 * Smooth(x);   // anticipation: drawing back before the jump
            sq = 0.16 * Smooth(x);
        } else {
            // Sticky: the body pulls away but the tail stays glued to the wall, stretching, then lets go.
            p = stuckFrom + (start - stuckFrom) * Smooth(x);
            stick = 1 - Smooth((x - 0.75) / 0.25);
            anchorX = from;
            anchorY = CY();
            SetFilm(liquid.films[0], EdgeOf(true, -dir), CY(), 1 - Smooth((x - 0.1) / 0.9), R * (0.9 + 1.1 * (1 - Smooth(x))));
            sq = -0.12 * Smooth(x);   // stretched thin by the pull
        }
    } else if (v < P2) {
        p = start + (approach - start) * Smoother((v - P1) / (P2 - P1));
    } else if (v < P3) {
        double x = (v - P2) / (P3 - P2);   // surface tension takes over: it accelerates into the wall
        p = approach + (stuckTo - approach) * x * x * x;
    } else {
        p = stuckTo;
        sq = Jiggle((v - P3) * dur, 0.22, 0.34);
    }
    if (v >= P2 - 0.05)
        SetFilm(liquid.films[1], EdgeOf(true, dir), CY(), Smooth((v - P2 + 0.05) / 0.1), R * (0.9 + 1.1 * Pop((v - P3) / 0.3)));
    px = p;
    py = CY();
    flatX = 1 - sq;
    flatY = 1 + sq * 0.7;
}

// From stuck on the wall at side, pulling free and gliding home, settling with a wobble.
void GuidedBreak::BackFromSide(int side, double v, double)
{
    double R = Radius(), wall = side < 0 ? 0 : width, stuck = wall - side * R * 0.8, pulled = wall - side * R * 1.9;
    axis = 0;
    if (v < 0.35) {
        double x = v / 0.35;
        px = stuck + (pulled - stuck) * Smooth(x);
        stick = 1 - Smooth((x - 0.7) / 0.3);
        anchorX = wall;
        anchorY = CY();
        SetFilm(liquid.films[0], EdgeOf(true, side), CY(), 1 - Smooth((x - 0.1) / 0.9), R * (0.9 + 1.1 * (1 - Smooth(x))));
        flatX = 1 + 0.12 * Smooth(x);
        lookTX = -side;
    } else {
        px = pulled + (CX() - pulled) * Pop((v - 0.35) / 0.65);
        lookTX = v < 0.7 ? -side : 0;
    }
    py = CY();
}

// Up and down: a glance down, then fall and get sucked back up (a movement), then drip back home.
void GuidedBreak::UpDown(double u, double len)
{
    Timing tm = Plan(len, 0.45, 0.9);
    if (u < tm.pre) {
        Glance(u, 0, 1);
        return;
    }
    double s = u - tm.pre;
    if (s < tm.moves) {
        int i = std::min(tm.reps - 1, static_cast<int>(s / tm.cycle));
        FallAndRise(i == 0, (s - i * tm.cycle) / tm.cycle, tm.cycle);
        return;
    }
    s -= tm.moves;
    if (s < tm.back) DripHome(s / tm.back);
}

// Falling (from the middle the first time, after that from the ceiling) under gravity, splatting on the floor, then
// sucked up to the ceiling with a thread still tied to the floor until it snaps, and hanging there.
void GuidedBreak::FallAndRise(bool first, double w, double c)
{
    double R = Radius(), floorY = height - R * 0.8, ceilY = R * 0.8;
    Liquid::Film& floor = liquid.films[0];
    Liquid::Film& ceiling = liquid.films[1];
    constexpr double F = 0.36, S = 0.52, U = 0.86;
    double y, sq = 0;
    axis = 1;
    if (w < F) {
        double x = w / F, y0 = first ? CY() : ceilY;
        y = y0 + (floorY - y0) * x * x;   // gravity
        lookTY = 1;
        if (!first) {
            stick = 1 - Smooth((x - 0.2) / 0.25);   // a drip: it hangs on by a neck, then lets go
            anchorX = CX();
            anchorY = 0;
            SetFilm(ceiling, 2, CX(), 1 - Smooth(x / 0.6), R * (0.9 + 1.1 * (1 - Smooth(x / 0.6))));
        }
    } else if (w < S) {
        y = floorY;
        sq = Jiggle((w - F) * c, 0.24, 0.4);
        lookTY = w < F + 0.6 * (S - F) ? 1 : -1;   // then it looks up: something is pulling
    } else if (w < U) {
        double x = (w - S) / (U - S);
        y = floorY + (ceilY - floorY) * std::pow(x, 2.2);   // sucked up, faster and faster
        stick = 1 - Smooth((x - 0.3) / 0.3);                 // a long thread back to the floor, then it snaps
        anchorX = CX();
        anchorY = height;
        SetFilm(floor, 3, CX(), 1 - Smooth((x - 0.05) / 0.55), R * (0.9 + 1.4 * (1 - Smooth(x / 0.6))));
        lookTY = -1;
        sq = -0.1 * Smooth(x / 0.4);
    } else {
        y = ceilY;
        sq = Jiggle((w - U) * c, 0.2, 0.34);
        lookTY = w > U + 0.06 ? 1 : -1;   // already looking down at where it will fall
    }
    if (w >= F - 0.04 && w < U)
        SetFilm(floor, 3, CX(), w < S ? Smooth((w - F + 0.04) / 0.08) : floor.a, w < S ? R * (0.9 + 1.4 * Pop((w - F) / 0.25)) : floor.spread);
    if (w >= U - 0.04) SetFilm(ceiling, 2, CX(), Smooth((w - U + 0.04) / 0.08), R * (0.9 + 1.1 * Pop((w - U) / 0.14)));
    px = CX();
    py = y;
    flatY = 1 - sq;
    flatX = 1 + sq * 0.7;
}

// From the ceiling, down on a thread that thins and snaps, settling in the middle.
void GuidedBreak::DripHome(double v)
{
    double R = Radius(), ceilY = R * 0.8, low = ceilY + R * 1.7;
    axis = 1;
    if (v < 0.4) {
        double x = v / 0.4;
        py = ceilY + (low - ceilY) * Smooth(x);
        stick = 1 - Smooth((x - 0.6) / 0.4);
        anchorX = CX();
        anchorY = 0;
        SetFilm(liquid.films[1], 2, CX(), 1 - Smooth(x), R * (0.9 + 1.1 * (1 - Smooth(x))));
        lookTY = 1;
    } else {
        py = low + (CY() - low) * Pop((v - 0.4) / 0.6);
        lookTY = v < 0.75 ? 1 : 0;
    }
}

// The circle: a glance up, a slide up to the top, laps round the screen leaving a snail trail, then home.
void GuidedBreak::Circle(double u, double len)
{
    constexpr double glance = 0.45, rise = 0.6;
    Timing tm = Plan(len, glance + rise, 0.8);
    double R = Radius(), rx = width / 2.0 - R * 2.2, ry = height / 2.0 - R * 2.2;
    if (u < glance) {
        Glance(u, 0, -1);
        return;
    }
    if (u < tm.pre) {
        double x = (u - glance) / rise;
        py = CY() - ry * Smoother(x);
        axis = 1;
        lookTY = x < 0.65 ? -1 : 0;
        lookTX = x < 0.65 ? 0 : 1;   // and already looking along the way round
        return;
    }
    double s = u - tm.pre;
    if (s < tm.moves) {
        double th = -Pi / 2 + 2 * Pi * tm.reps * Cruise(s / tm.moves, std::min(0.3, 0.6 / tm.moves));
        px = CX() + rx * std::cos(th);
        py = CY() + ry * std::sin(th);
        double vx = -rx * std::sin(th), vy = ry * std::cos(th), vl = std::max(1e-6, std::sqrt(vx * vx + vy * vy));
        lookTX = vx / vl;
        lookTY = vy / vl;
        axis = 2;
        stiff = 1.6;
        trailOn = true;   // only while going round
        return;
    }
    s -= tm.moves;
    if (s < tm.back) {
        double x = s / tm.back;
        py = (CY() - ry) + ry * Pop(x);
        axis = 1;
        lookTY = x < 0.5 ? 1 : 0;
    }
}

// Closing: it becomes a real eye, blinks slowly (shut, a moment, open with a little widening), then melts back into
// the drop.
void GuidedBreak::Blink(double u, double len)
{
    Timing tm = Plan(len, 0.6, 0.6);
    if (u < tm.pre) {
        eyeShape = Smooth(u / tm.pre);
        return;
    }
    double s = u - tm.pre;
    if (s < tm.moves) {
        eyeShape = 1;
        int i = std::min(tm.reps - 1, static_cast<int>(s / tm.cycle));
        double w = (s - i * tm.cycle) / tm.cycle;
        open = w < 0.34 ? 1 - Smooth(w / 0.34) : w < 0.52 ? 0 : Pop((w - 0.52) / 0.48);
        double shut = 1 - std::min(1.0, open);
        lookTY = 0.2 * shut;       // the eye rolls down a little behind the lid
        flatY = 1 - 0.06 * shut;   // and the lid's weight settles the drop a touch
        flatX = 1 + 0.04 * shut;
        py += Radius() * 0.05 * shut;
        return;
    }
    s -= tm.moves;
    eyeShape = s < tm.back ? 1 - Smooth(s / tm.back) : 0;
}

// ---- Drawing ----

// The liquid is shown in its own see-through window, moved and sized to just the area it covers each frame, so it
// floats over the black background or, with that off, over the screen itself.
void GuidedBreak::Redraw()
{
    auto opacity = static_cast<BYTE>(255 * fadeK);
    if (alpha > 0.002f) {
        liquid.Render({width, height}, alpha);
        liquid.Present(drop->Handle(), {bounds.left, bounds.top}, opacity);
        liquidShown = true;
    } else if (liquidShown) {
        liquid.Present(drop->Handle(), {bounds.left, bounds.top}, 0);
        liquidShown = false;
    }
}

// The step's words and the time left, in a see-through window near the bottom. Over the screen (no dark background)
// they sit on a soft dark band so they stay readable.
void GuidedBreak::UpdateCaption()
{
    float wd = width / scale, captionH = 14 * 96 / 72.0f * 1.35f, hintH = 10 * 96 / 72.0f * 1.35f, hd = captionH * 1.4f + hintH * 1.6f + 16;
    ID2D1RenderTarget* t = captionArt.Begin(width, static_cast<int>(std::ceil(hd * scale)), scale);
    if (!t) return;
    if (step >= 0 && outroAt < 0) {
        std::wstring text = Lang::T(Steps[step].text);
        std::wstring left = std::to_wstring(std::max(0, shownSeconds));
        std::wstring hint = forced ? left : Lang::F(L"Esc to skip  ·  {0}", {left});
        if (!backdrop) {
            // The band: as wide as the longer line, plus some room.
            gfx::ComPtr<IDWriteTextLayout> a, b;
            DWRITE_TEXT_METRICS ma{}, mb{};
            gfx::Write()->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), captionFont.Get(), 10000, 100, a.GetAddressOf());
            gfx::Write()->CreateTextLayout(hint.c_str(), static_cast<UINT32>(hint.size()), hintFont.Get(), 10000, 100, b.GetAddressOf());
            if (a) a->GetMetrics(&ma);
            if (b) b->GetMetrics(&mb);
            float tw = std::max(ma.width, mb.width) + captionH * 1.6f;
            gfx::Fill(t, {{(wd - tw) / 2, 0, (wd + tw) / 2, hd}, hd * 0.3f, hd * 0.3f}, Palette::Argb(12, 14, 18), 150 / 255.0f);
        }
        gfx::Text(t, text, captionFont.Get(), {0, 8, wd, 8 + captionH * 1.4f}, backdrop ? Palette::Argb(118, 118, 118) : Palette::Argb(222, 226, 232),
                  gfx::Align::Center, true);
        gfx::Text(t, hint, hintFont.Get(), {0, 8 + captionH * 1.5f, wd, 8 + captionH * 1.5f + hintH * 1.4f},
                  backdrop ? Palette::Argb(70, 70, 70) : Palette::Argb(160, 166, 176), gfx::Align::Center, true);
    }
    captionArt.End();
    ShowCaption();
}

void GuidedBreak::ShowCaption()
{
    if (!caption || !captionArt.Width()) return;
    POINT at{bounds.left, bounds.bottom - captionArt.Height() - height / 14};
    captionArt.Present(caption->Handle(), at, static_cast<BYTE>(255 * fadeK));
}

} // namespace bb
