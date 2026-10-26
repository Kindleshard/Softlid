#include "features/mini/MiniWindow.h"
#include "features/mini/MiniHost.h"
#include "core/ColorMath.h"
#include "core/Lang.h"
#include "platform/Activity.h"
#include "ui/DirectWindow.h"
#include "ui/MiniArt.h"
#include "ui/Theme.h"
#include "ui/controls/Basic.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace bb {
namespace {

using namespace ui;

constexpr UINT_PTR HoverTimer = 1, FadeTimer = 2, AnimTimer = 3;
constexpr ULONGLONG NotIdle = ~0ull;
constexpr double Faded = 166;          // 65% of 255
constexpr double BreathSeconds = 3.6;  // one breath of the glow
constexpr double FlowSeconds = 1.7;    // one run of the focus timer's highlight, splash included
constexpr float Pi = std::numbers::pi_v<float>;

gfx::ComPtr<ID2D1StrokeStyle> RoundCaps()
{
    gfx::ComPtr<ID2D1StrokeStyle> s;
    D2D1_STROKE_STYLE_PROPERTIES p{};
    p.startCap = p.endCap = D2D1_CAP_STYLE_ROUND;
    gfx::D2D()->CreateStrokeStyle(p, nullptr, 0, s.GetAddressOf());
    return s;
}

// An arc of a circle, from `start` degrees (0 at the right, clockwise) for `sweep` degrees; too short to see: none.
void Arc(ID2D1RenderTarget* t, float cx, float cy, float r, float start, float sweep, ID2D1Brush* b, float width, ID2D1StrokeStyle* s)
{
    if (sweep < 0.5f) return;
    if (sweep >= 359.9f) {
        t->DrawEllipse({{cx, cy}, r, r}, b, width);
        return;
    }
    auto at = [&](float deg) { return D2D1_POINT_2F{cx + r * std::cos(deg * Pi / 180), cy + r * std::sin(deg * Pi / 180)}; };
    gfx::ComPtr<ID2D1PathGeometry> path;
    gfx::ComPtr<ID2D1GeometrySink> sink;
    gfx::D2D()->CreatePathGeometry(path.GetAddressOf());
    path->Open(sink.GetAddressOf());
    sink->BeginFigure(at(start), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc({at(start + sweep), {r, r}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, sweep > 180 ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL});
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    t->DrawGeometry(path.Get(), b, width, s);
}

std::wstring Kelvin(int blue) { return blue == 0 ? Lang::T(L"Off") : std::to_wstring(ColorMath::KelvinFor(blue)) + L" K"; }

} // namespace

// ---- The panel: a rounded card with the quick controls ----

class MiniWindow::Panel : public DirectWindow {
public:
    static constexpr float W = 300;

    explicit Panel(MiniHost& app) : app(app), s(app.S())
    {
        brightLabel = &Text(L"Brightness", 16, 10, 180, 18, Theme::Fg);
        brightText = &Text(L"", 196, 10, 90, 18, Theme::Dim);
        brightText->align = HAlign::Right;
        bright = &Root().Add<Slider>(20, 100, s.Brightness);
        bright->SetBounds(8, 28, 286, 28);
        blueLabel = &Text(L"Blue light filter", 16, 60, 180, 18, Theme::Fg);
        blueText = &Text(L"", 196, 60, 90, 18, Theme::Dim);
        blueText->align = HAlign::Right;
        blue = &Root().Add<Slider>(s.CoolRange ? -75 : 0, 100, s.BlueFilter);
        blue->SetBounds(8, 78, 286, 28);
        blue->warm = true;
        lightLabel = &Text(L"Display backlight", 16, 110, 180, 18, Theme::Fg);
        lightText = &Text(L"", 196, 110, 90, 18, Theme::Dim);
        lightText->align = HAlign::Right;
        light = &Root().Add<Slider>(0, 100, std::max(0, s.Backlight));
        light->SetBounds(8, 128, 286, 28);
        blinkLabel = &Text(L"Blink reminders", 16, 166, 180, 24, Theme::Fg);
        blink = &Root().Add<Toggle>(s.BlinkEnabled);
        blink->SetBounds(244, 167, 44, 22);
        status = &Text(L"", 16, 200, 268, 20, Theme::Dim);
        focusPause = &Root().Add<Button>();
        focusPause->SetGlyph(L'\uE768');
        focusPause->focusable = false;
        focusStop = &Root().Add<Button>();
        focusStop->SetGlyph(L'\uE71A');
        focusStop->focusable = false;
        focusStop->tip = Lang::T(L"Stop");

        bright->onChange = [this](int v) {
            s.Brightness = v;
            this->app.ManualScreenChange();
            s.FilterEnabled = true;
            this->app.Changed(true);
            ShowValues();
        };
        blue->onChange = [this](int v) {
            s.BlueFilter = v;
            s.ColorEffect = ColorMath::NoEffect;   // a colour effect and the blue light filter take turns
            blue->SetMuted(false);
            this->app.ManualScreenChange();
            s.FilterEnabled = true;
            this->app.PreviewNight();
            this->app.Changed(true);
            ShowValues();
        };
        light->onChange = [this](int v) {
            s.Backlight = v;
            this->app.Changed(true);
            ShowValues();
        };
        blink->onChange = [this](bool on) {
            s.BlinkEnabled = on;
            this->app.Changed();
        };
        focusPause->onClick = [this] {
            if (this->app.FocusState() == Focus::Working) this->app.FocusPause();
            else this->app.FocusStart();
            ShowStatus();
        };
        focusStop->onClick = [this] {
            this->app.FocusStop();
            ShowStatus();
        };
    }

    // Opens it (hidden) once, so it can be sized and placed before it shows.
    void Make()
    {
        if (Handle()) return;
        POINT away{-30000, 0};
        Options opts;
        opts.popup = true;
        Open(W, 230, &away, opts);
        // Fades out as a whole (the window's own opacity), and stays above other windows like the icon.
        SetWindowLongPtrW(Handle(), GWL_EXSTYLE, GetWindowLongPtrW(Handle(), GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TOPMOST);
        SetOpacity(1);
    }

    // Hotkeys and the settings may have changed things since it was last open. The backlight row shows only while
    // the real backlight is in use; the rows under it close up when it isn't.
    void Sync()
    {
        bright->SetValue(s.Brightness);
        blue->SetRange(s.CoolRange ? -75 : 0, 100);
        blue->SetValue(s.BlueFilter);
        blue->SetMarker(s.ScheduleEnabled ? s.NightBlue : -1);   // the night schedule's strength
        blue->SetMuted(s.FilterEnabled && s.ColorEffect != ColorMath::NoEffect && app.EffectsAvailable());
        blink->SetOn(s.BlinkEnabled);
        light->SetValue(std::max(0, s.Backlight));
        bool hw = s.HardwareBrightness;
        lightLabel->SetVisible(hw);
        lightText->SetVisible(hw);
        light->SetVisible(hw);
        float y = hw ? 166.0f : 116.0f;
        blinkLabel->SetBounds(16, y, 180, 24);
        blink->SetBounds(244, y + 1, 44, 22);
        status->SetBounds(16, y + 36, 268, 20);
        height = y + 72;
        ShowValues();
        ShowStatus();
    }

    // The line at the bottom: the time to the next break, or what the focus timer is at - with its buttons beside it:
    // pause (or resume) and stop while it runs, and while it is off a play button to start it. (None during its rest:
    // the rest card is up then.)
    void ShowStatus()
    {
        Focus focus = app.FocusState();
        status->SetPlainText(focus != Focus::Off ? Lang::F(L"Focus timer: {0}", {app.FocusText()}) : app.BreakStatus());
        bool on = focus == Focus::Working || focus == Focus::Paused;
        float y = status->Bounds().top - 3;
        focusStop->SetVisible(on);
        focusStop->SetBounds(260, y, 28, 28);
        focusPause->SetVisible(focus != Focus::Resting);
        focusPause->SetBounds(on ? 260.0f - 28 - 6 : 260.0f, y, 28, 28);
        focusPause->SetGlyph(focus == Focus::Working ? L'\uE769' : L'\uE768');   // pause : play
        focusPause->tip = Lang::T(focus == Focus::Working ? L"Pause" : focus == Focus::Paused ? L"Resume" : L"Start the focus timer");
        float right = focusPause->Visible() ? focusPause->Bounds().left - 6 : W - 16;
        status->SetBounds(16, status->Bounds().top, right - 16, 20);
    }

    float Height() const { return height; }

    void SetOpacity(double k)
    {
        opacity = k;
        SetLayeredWindowAttributes(Handle(), 0, static_cast<BYTE>(std::clamp(k, 0.0, 1.0) * 255), LWA_ALPHA);
    }
    double Opacity() const { return opacity; }
    bool Showing() const { return Handle() && IsWindowVisible(Handle()); }

private:
    Label& Text(std::wstring_view english, float x, float y, float w, float h, int color)
    {
        Label& l = Root().Add<Label>(english, Fonts::Small(), color);
        l.SetBounds(x, y, w, h);
        return l;
    }

    void ShowValues()
    {
        brightText->SetPlainText(std::to_wstring(s.Brightness) + L"%");
        blueText->SetPlainText(Kelvin(s.BlueFilter));
        lightText->SetPlainText(std::to_wstring(std::max(0, s.Backlight)) + L"%");
    }

    MiniHost& app;
    Settings& s;
    Slider *bright, *blue, *light;
    Label *brightLabel, *brightText, *blueLabel, *blueText, *lightLabel, *lightText, *blinkLabel, *status;
    Toggle* blink;
    Button *focusPause, *focusStop;
    float height = 230;
    double opacity = 1;
};

// ---- The icon ----

MiniWindow::MiniWindow(MiniHost& app) : app(app), panel(std::make_unique<Panel>(app))
{
    POINT cursor;
    GetCursorPos(&cursor);
    scale = ScaleAt(cursor);
    px = static_cast<int>(std::lround(size * scale));
    pos = StartingPoint();
    Create(L"BlinkBuddy.Mini", WS_POPUP, PopupExStyle | WS_EX_LAYERED, {pos.x, pos.y, pos.x + px, pos.y + px});
    Draw();
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    nextBreath = GetTickCount64() + static_cast<ULONGLONG>(std::max(3, app.S().MiniOrbitSeconds)) * 1000;
    SetTimer(hwnd, HoverTimer, 120, nullptr);
    idleSince = GetTickCount64();   // nobody is hovering yet: fade after the delay
    fading = true;
    SetTimer(hwnd, FadeTimer, 15, nullptr);
}

MiniWindow::~MiniWindow()
{
    panel.reset();
    Destroy();
}

// Where it was left, if that is still on a screen; otherwise near the top-right corner of the main screen.
POINT MiniWindow::StartingPoint() const
{
    const Settings& s = app.S();
    RECT want{s.MiniX, s.MiniY, s.MiniX + px, s.MiniY + px};
    MONITORINFO mi{sizeof mi};
    if (s.MiniX >= 0 && GetMonitorInfoW(MonitorFromRect(&want, MONITOR_DEFAULTTONULL), &mi)) {
        RECT inside;
        IntersectRect(&inside, &want, &mi.rcWork);
        if (EqualRect(&inside, &want)) return {s.MiniX, s.MiniY};
    }
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    return {mi.rcWork.right - px - px / 2, mi.rcWork.top + px / 2};
}

RECT MiniWindow::Bounds() const { return {pos.x, pos.y, pos.x + px, pos.y + px}; }

// What the lamp and the laptop show: the screen's brightness (0 at its lowest, 1 at full).
float MiniWindow::Bright() const { return app.S().FilterEnabled ? (app.S().Brightness - 20) / 80.0f : 1.0f; }
bool MiniWindow::FocusOn() const { return app.FocusState() == Focus::Working || app.FocusState() == Focus::Paused; }
bool MiniWindow::Flowing() const { return app.FocusState() == Focus::Working; }
float MiniWindow::Progress() const { return app.S().MiniCountdown || FocusOn() ? app.BreakProgress() : -1; }

// A dark disc with a soft rim (the accent while hovered), the chosen icon inside in its current state (or the focus
// timer's time), the arc of the wait for the next break, and the breathing glow; the window's own fade is separate.
void MiniWindow::Draw()
{
    ID2D1RenderTarget* t = canvas.Begin(px, px, scale);
    if (!t) return;
    const Settings& s = app.S();
    float k = static_cast<float>(awake * awake * (3 - 2 * awake));
    drawnBright = Bright();
    drawnWarm = app.WarmthNow();
    drawnIcon = s.MiniIcon;
    drawnProgress = Progress();
    float ring = std::max(1.5f, size / 30), cx = size / 2, cy = size / 2, r = (size - ring - 1) / 2;
    gfx::ComPtr<ID2D1SolidColorBrush> brush;
    t->CreateSolidColorBrush(gfx::Color(Palette::Argb(34, 36, 42), 242 / 255.0f), brush.GetAddressOf());
    t->FillEllipse({{cx, cy}, r, r}, brush.Get());
    D2D1_COLOR_F rest = gfx::Color(Palette::Argb(90, 96, 110), 120 / 255.0f), on = gfx::Color(Theme::Accent());
    brush->SetColor({rest.r + (on.r - rest.r) * k, rest.g + (on.g - rest.g) * k, rest.b + (on.b - rest.b) * k, rest.a + (on.a - rest.a) * k});
    t->DrawEllipse({{cx, cy}, r, r}, brush.Get(), ring);
    float box = size * 0.62f;
    drawnClock = FocusOn() ? app.FocusText() : L"";
    if (!drawnClock.empty()) DrawClock(t, drawnClock);   // the focus timer's countdown in place of the icon
    else MiniArt::DrawIcon(t, {(size - box) / 2, (size - box) / 2, (size + box) / 2, (size + box) / 2}, s.MiniIcon, k, drawnBright, drawnWarm);
    // The wait for the next break (or the end of a focus period), as an arc filling round the rim from the top.
    if (drawnProgress > 0.002f) {
        brush->SetColor(gfx::Color(Theme::Accent(), 190 / 255.0f));
        Arc(t, cx, cy, r, -90, 360 * drawnProgress, brush.Get(), ring * 1.15f, RoundCaps().Get());
        if (Flowing()) DrawFlow(t, cx, cy, r, ring, drawnProgress);
    }
    if (!FocusOn()) DrawBreath(t, cx, cy, r, ring);   // (the focus timer's flow takes the breath's place)
    canvas.End();
    canvas.Present(hwnd, pos, static_cast<BYTE>(std::lround(shown)));
}

// The focus timer's time left, in the middle; paused, it is dimmed with a small pause sign under it.
void MiniWindow::DrawClock(ID2D1RenderTarget* t, const std::wstring& time)
{
    bool paused = app.FocusState() == Focus::Paused;
    if (!clockFont) {   // (made once: this is drawn up to 30 times a second)
        clockFont = gfx::Font(size * 0.23f * 0.75f, gfx::Weight::Semibold, L"Segoe UI");
        clockFontBig = gfx::Font(size * 0.27f * 0.75f, gfx::Weight::Semibold, L"Segoe UI");
    }
    float shift = paused ? -size * 0.06f : 0;
    gfx::Text(t, time, (time.size() > 4 ? clockFont : clockFontBig).Get(), {0, shift, size, size + shift}, paused ? Theme::Dim : Theme::Fg,
              gfx::Align::Center);
    if (!paused) return;
    auto pause = gfx::Font(size * 0.13f * 0.75f, gfx::Weight::Normal, L"Segoe Fluent Icons");
    gfx::Text(t, L"\uE769", pause.Get(), {0, size * 0.56f, size, size * 0.76f}, Theme::Dim, gfx::Align::Center);
}

// While the focus timer runs, a highlight flows along the filled arc like light caught in a liquid: it gathers at the
// top, runs faster and faster (stretching as it goes) to the arc's end, splashes against it, and starts again.
void MiniWindow::DrawFlow(ID2D1RenderTarget* t, float cx, float cy, float r, float ring, float progress)
{
    double now = GetTickCount64() / 1000.0;
    double phase = std::fmod(now, FlowSeconds) / FlowSeconds;
    float sweep = 360 * progress;
    int glint = Mix(Theme::Accent(), Palette::Argb(255, 255, 255), 2 / 3.0f);
    auto round = RoundCaps();
    gfx::ComPtr<ID2D1SolidColorBrush> b;
    t->CreateSolidColorBrush(gfx::Color(glint), b.GetAddressOf());
    constexpr double run = 0.7;
    if (phase < run) {
        double u = phase / run;
        float head = sweep * static_cast<float>(u * u);                              // accelerating, like a drop running down
        float trail = std::min(sweep, 80.0f) * static_cast<float>(0.35 + 0.65 * u);   // longer the faster it goes
        float fadeIn = static_cast<float>(std::min(1.0, u * 6));
        constexpr int parts = 10;
        for (int i = 0; i < parts; i++) {
            float end = head - trail * i / parts, start = std::max(0.0f, head - trail * (i + 1) / parts);
            if (end <= start) break;
            float k = 1 - i / static_cast<float>(parts);
            b->SetColor(gfx::Color(glint, 235 / 255.0f * k * k * fadeIn));
            Arc(t, cx, cy, r, -90 + start, end - start, b.Get(), ring * (0.7f + 0.5f * k), round.Get());
        }
        return;
    }
    // The splash at the end: a bright bead that swells into a fading ring, and the arc's end lit for a moment.
    float s1 = static_cast<float>((phase - run) / (1 - run)), fade = (1 - s1) * (1 - s1);
    float angle = (-90 + sweep) * Pi / 180, x = cx + std::cos(angle) * r, y = cy + std::sin(angle) * r;
    float tip = std::min(sweep, 16.0f);
    b->SetColor(gfx::Color(glint, 220 / 255.0f * fade));
    Arc(t, cx, cy, r, -90 + sweep - tip, tip, b.Get(), ring * 1.2f, round.Get());
    float bead = ring * (1.1f - 0.4f * s1), wave = ring * (1.2f + 2.8f * s1);
    b->SetColor(gfx::Color(glint, 240 / 255.0f * fade));
    t->FillEllipse({{x, y}, bead, bead}, b.Get());
    b->SetColor(gfx::Color(glint, 170 / 255.0f * fade));
    t->DrawEllipse({{x, y}, wave, wave}, b.Get(), std::max(1.0f, ring * 0.6f));
}

// Now and then the icon breathes: its rim swells into a soft glow, which spreads a little way inward, and eases back
// out - a slow breath in, a longer breath out.
void MiniWindow::DrawBreath(ID2D1RenderTarget* t, float cx, float cy, float r, float ring)
{
    if (!breathStart) return;
    double p = (GetTickCount64() - breathStart) / 1000.0 / BreathSeconds;
    if (p >= 1) return;
    constexpr double inhale = 0.4;
    float breath = static_cast<float>(p < inhale ? 0.5 - 0.5 * std::cos(Pi * p / inhale) : 0.5 + 0.5 * std::cos(Pi * (p - inhale) / (1 - inhale)));
    if (breath < 0.01f) return;
    int c = app.S().MiniOrbitColor == 0 ? Theme::Accent() : app.S().MiniOrbitColor | 0xFF000000;
    // The glow inside the rim, fading toward the middle so the icon stays clear.
    D2D1_GRADIENT_STOP stops[] = {{0, gfx::Color(c, 0)}, {0.62f, gfx::Color(c, 0)}, {1, gfx::Color(c, 110 / 255.0f * breath)}};
    gfx::ComPtr<ID2D1GradientStopCollection> collection;
    gfx::ComPtr<ID2D1RadialGradientBrush> glow;
    t->CreateGradientStopCollection(stops, 3, collection.GetAddressOf());
    t->CreateRadialGradientBrush({{cx, cy}, {0, 0}, r, r}, collection.Get(), glow.GetAddressOf());
    t->FillEllipse({{cx, cy}, r, r}, glow.Get());
    // And the rim itself, brighter and a little fuller.
    gfx::ComPtr<ID2D1SolidColorBrush> b;
    t->CreateSolidColorBrush(gfx::Color(c, 235 / 255.0f * breath), b.GetAddressOf());
    t->DrawEllipse({{cx, cy}, r, r}, b.Get(), ring * (1 + 0.7f * breath));
}

void MiniWindow::StartAnim()
{
    lastAnim = GetTickCount64();
    if (animating) return;
    animating = true;
    SetTimer(hwnd, AnimTimer, 15, nullptr);
}

void MiniWindow::SetLit(bool on)
{
    if (lit == on) return;
    lit = on;
    StartAnim();
}

// Moves the hover animation toward lit (about a quarter of a second) and keeps the breath or the flow going.
void MiniWindow::AnimStep()
{
    ULONGLONG now = GetTickCount64();
    double dt = std::min(0.05, (now - lastAnim) / 1000.0);
    lastAnim = now;
    double target = lit ? 1 : 0, speed = lit ? 4.5 : 2.6;   // wakes quickly, settles back more slowly
    awake = target > awake ? std::min(target, awake + speed * dt) : std::max(target, awake - speed * dt);
    bool breathing = breathStart && (now - breathStart) / 1000.0 < BreathSeconds;
    if (!breathing) breathStart = 0;
    bool flowing = Flowing() && drawnProgress > 0.002f;
    Draw();
    // The hover animation at 60 frames a second; a slow breath or the flow at 30.
    SetTimer(hwnd, AnimTimer, awake != target ? 15 : 33, nullptr);
    if (awake == target && !breathing && !flowing) {
        KillTimer(hwnd, AnimTimer);
        animating = false;
    }
}

// Back to full strength at once when the pointer arrives (the panel too, if it was fading out).
void MiniWindow::Wake()
{
    idleSince = NotIdle;
    if (shown < 255) {
        shown = 255;
        Draw();
    }
    if (panel->Showing() && panel->Opacity() < 1) panel->SetOpacity(1);
    if (!panel->Showing() && fading) {
        KillTimer(hwnd, FadeTimer);
        fading = false;
    }
}

// When the pointer has left: after the delay set in Options, the panel fades out over the time set there (and
// closes), and the icon fades to 65% with it.
void MiniWindow::Rest()
{
    if (idleSince != NotIdle) return;
    idleSince = GetTickCount64();
    fading = true;
    SetTimer(hwnd, FadeTimer, 15, nullptr);
}

// Every 15 ms while the panel is open or a fade is due: watches the pointer closely (so a short delay is really
// short), and moves the fade along.
void MiniWindow::FadeStep()
{
    const Settings& s = app.S();
    if (panel->Showing() && !pressed) {
        POINT at;
        GetCursorPos(&at);
        RECT eye = Bounds(), around;
        GetWindowRect(panel->Handle(), &around);
        int g = static_cast<int>(gap * scale);
        InflateRect(&around, g, g);
        bool here = PtInRect(&eye, at) || PtInRect(&around, at) || GetKeyState(VK_LBUTTON) < 0;   // a slider being dragged keeps it open
        if (here) {
            Wake();
            return;
        }
        Rest();
    }
    if (idleSince == NotIdle) return;
    double since = static_cast<double>(GetTickCount64() - idleSince) - s.MiniFadeDelay;
    if (since < 0) return;
    double k = s.MiniFadeTime <= 0 ? 1 : std::min(1.0, since / s.MiniFadeTime), e = k * k * (3 - 2 * k);
    shown = 255 - (255 - Faded) * e;
    Draw();
    if (panel->Showing()) {
        if (k >= 1) {
            ShowWindow(panel->Handle(), SW_HIDE);
            panel->SetOpacity(1);
            SetLit(false);
        } else {
            panel->SetOpacity(1 - e);
        }
    }
    if (k >= 1) {
        KillTimer(hwnd, FadeTimer);
        fading = false;
    }
}

void MiniWindow::CheckHover()
{
    const Settings& s = app.S();
    ULONGLONG now = GetTickCount64();
    // The breath, when it is due.
    if (s.MiniOrbit && !FocusOn() && now >= nextBreath) {
        nextBreath = now + static_cast<ULONGLONG>((s.MiniOrbitSeconds == 0 ? BreathSeconds : s.MiniOrbitSeconds) * 1000);   // 0: right on
        breathStart = now;
        StartAnim();
    }
    // The focus timer's flow keeps the animation going while it runs.
    if (Flowing() && !animating) StartAnim();
    // Brightness, the filter or the chosen icon changed (shortcuts, the panel, the schedule): show it.
    if (!animating && (std::abs(Bright() - drawnBright) > 0.005f || std::abs(app.WarmthNow() - drawnWarm) > 0.005f ||
                       s.MiniIcon != drawnIcon || std::abs(Progress() - drawnProgress) > 0.004f ||
                       (FocusOn() ? app.FocusText() : L"") != drawnClock))
        Draw();
    // Stay above other windows, even ones that made themselves topmost since.
    if (++tick % 16 == 0) SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (pressed) return;
    POINT at;
    GetCursorPos(&at);
    RECT eye = Bounds();
    bool onEye = PtInRect(&eye, at) != FALSE, onPanel = false;
    if (panel->Showing()) {
        RECT around;
        GetWindowRect(panel->Handle(), &around);
        int g = static_cast<int>(gap * scale);
        InflateRect(&around, g, g);
        onPanel = PtInRect(&around, at) != FALSE;
    }
    SetLit(onEye || onPanel);
    if (onEye || onPanel) Wake();
    if (onEye && !panel->Showing()) OpenPanel();
    if (panel->Showing()) panel->ShowStatus();
    if (!panel->Showing() && !onEye) Rest();
    // (While the panel is open, FadeStep watches the pointer and fades it out.)
}

// Beside the icon, toward the middle of the screen, centred on it vertically.
void MiniWindow::OpenPanel()
{
    panel->Make();
    panel->Sync();
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorFromPoint(pos, MONITOR_DEFAULTTONEAREST), &mi);
    RECT wa = mi.rcWork;
    float ps = panel->Scale();
    int w = static_cast<int>(Panel::W * ps), h = static_cast<int>(panel->Height() * ps), g = static_cast<int>(gap * scale);
    bool left = pos.x + px / 2 > (wa.left + wa.right) / 2;
    int x = left ? pos.x - g - w : pos.x + px + g;
    int y = std::clamp(static_cast<int>(pos.y + px / 2 - h / 2), static_cast<int>(wa.top), static_cast<int>(wa.bottom - h));
    SetWindowPos(panel->Handle(), HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    panel->SetOpacity(1);
    idleSince = NotIdle;
    fading = true;
    SetTimer(hwnd, FadeTimer, 15, nullptr);
}

LRESULT MiniWindow::OnMessage(UINT message, WPARAM w, LPARAM l)
{
    switch (message) {
    case WM_TIMER:
        if (w == HoverTimer) CheckHover();
        else if (w == FadeTimer) FadeStep();
        else if (w == AnimTimer) AnimStep();
        return 0;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
    case WM_MOUSEMOVE:
        if (!pressed) {
            Wake();
            return 0;
        }
        {
            POINT now;
            GetCursorPos(&now);
            if (!dragged && std::abs(now.x - grab.x) + std::abs(now.y - grab.y) < 5) return 0;
            if (!dragged && panel->Showing()) ShowWindow(panel->Handle(), SW_HIDE);
            dragged = true;
            pos.x += now.x - grab.x;
            pos.y += now.y - grab.y;
            grab = now;
            SetWindowPos(hwnd, nullptr, pos.x, pos.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    case WM_LBUTTONDOWN:
        pressed = true;
        dragged = false;
        GetCursorPos(&grab);
        SetCapture(hwnd);
        return 0;
    case WM_LBUTTONUP:
        if (!pressed) return 0;
        pressed = false;
        ReleaseCapture();
        if (dragged) app.SaveMiniPosition(pos);
        else app.LeaveMiniMode();   // a plain click on the icon opens the full settings
        return 0;
    case WM_CAPTURECHANGED:
        pressed = false;
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    }
    return Window::OnMessage(message, w, l);
}

} // namespace bb
