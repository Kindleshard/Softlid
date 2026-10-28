#include "features/reminders/BreakCard.h"
#include "core/Lang.h"
#include "features/reminders/Chime.h"
#include "platform/Activity.h"
#include "platform/DarkMode.h"
#include "ui/Access.h"
#include "ui/EyeArt.h"
#include "ui/Theme.h"
#include <algorithm>
#include <windowsx.h>

namespace bb {
namespace {

constexpr UINT_PTR StepTimer = 1;
constexpr float CardW = 400, CardH = 216;   // in 96-dpi units
const D2D1_RECT_F Buttons[2] = {{160, 166, 280, 200}, {288, 166, 376, 200}};   // Snooze 5 min, Skip

} // namespace

BreakCard::BreakCard(HWND owner, bool standUp, const Settings& s, int seconds, std::wstring caption, std::wstring note)
    : owner(owner), standUp(standUp), sounds(s.Sounds), forced(s.ForceRest)
{
    total = left = seconds > 0 ? seconds : standUp ? s.LongBreakDuration * 60 : s.BreakDuration;
    title = !caption.empty() ? caption : Lang::T(standUp ? L"Time for a stand-up break" : L"Look away from the screen");
    hint = !note.empty() ? note : Lang::T(standUp ? L"Get up, stretch, walk around, maybe refill your water."
                                                  : L"Find something at least 6 m (20 ft) away and let your eyes relax.");

    scale = ScaleAtCursor();
    RECT wa = WorkAreaAtCursor();
    int w = static_cast<int>(CardW * scale), h = static_cast<int>(CardH * scale);
    at = {wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2};

    // The dimmed screen goes up first, so the card lands above it.
    if (s.BreakBackdrop) backdrop.Show(0.85f);
    Create(L"BlinkBuddy.BreakCard", WS_POPUP, PopupExStyle | WS_EX_LAYERED, {at.x, at.y, at.x + w, at.y + h});
    Render();
    ShowOnTop(hwnd);
    ui::Access::Announce(hwnd, title + L". " + hint);
    if (sounds) Chime::Play(false);
    SetTimer(hwnd, StepTimer, 1000, nullptr);
}

std::wstring BreakCard::Format(int seconds) const
{
    if (total < 60) return std::to_wstring(seconds);
    wchar_t text[16];
    swprintf_s(text, L"%d:%02d", seconds / 60, seconds % 60);
    return text;
}

LRESULT BreakCard::OnMessage(UINT message, WPARAM w, LPARAM l)
{
    switch (message) {
    case WM_TIMER:
        if (w == StepTimer) Step();
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;   // clicking a button doesn't take focus from what you were doing
    case WM_MOUSEMOVE: {
        int now = ButtonAt({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
        if (now != hover) { hover = now; Render(); }
        TRACKMOUSEEVENT track{sizeof track, TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&track);
        return 0;
    }
    case WM_MOUSELEAVE:
        hover = down = -1;
        Render();
        return 0;
    case WM_LBUTTONDOWN:
        down = ButtonAt({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
        Render();
        return 0;
    case WM_LBUTTONUP: {
        int clicked = ButtonAt({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
        if (clicked >= 0 && clicked == down) Finish(clicked == 0 ? 300 : 0);
        down = -1;
        Render();
        return 0;
    }
    }
    return Window::OnMessage(message, w, l);
}

int BreakCard::ButtonAt(POINT p) const
{
    if (forced || over) return -1;
    float x = p.x / scale, y = p.y / scale;
    for (int i = 0; i < 2; i++)
        if (x >= Buttons[i].left && x < Buttons[i].right && y >= Buttons[i].top && y < Buttons[i].bottom) return i;
    return -1;
}

void BreakCard::Step()
{
    if (over) { Finish(0); return; }
    if (--left > 0) { Render(); return; }
    // You're looking away, so the end is announced by sound and the card lingers briefly.
    over = true;
    title = Lang::T(L"Welcome back!");
    hint = Lang::T(L"Break done. Blink a few times and carry on.");
    if (sounds) Chime::Play(true);
    SetTimer(hwnd, StepTimer, 2500, nullptr);
    Render();
}

void BreakCard::Finish(int snoozeSeconds)
{
    if (finished) return;
    finished = true;
    KillTimer(hwnd, StepTimer);
    backdrop.Hide();
    ShowWindow(hwnd, SW_HIDE);
    PostMessageW(owner, DoneMessage, standUp ? 1 : 0, snoozeSeconds);
}

void BreakCard::Render()
{
    ID2D1RenderTarget* t = canvas.Begin(static_cast<int>(CardW * scale), static_cast<int>(CardH * scale), scale);
    if (!t) return;
    gfx::Fill(t, {{0, 0, CardW, CardH}, 8, 8}, Theme::Card);
    gfx::Outline(t, {{0.5f, 0.5f, CardW - 0.5f, CardH - 0.5f}, 8, 8}, Theme::Line);

    // The eye (shut once the break is over), the words, and the countdown.
    EyeArt::Draw(t, {18 + 4, 16 + 4.4f, 18 + 60, 16 + 4.4f + 27.3f}, over ? 0.0f : 0.9f, 0, 0, 2.2f);
    auto titleFont = gfx::Font(11.5f, gfx::Weight::Semibold), hintFont = gfx::Font(8.75f), countFont = gfx::Font(28, gfx::Weight::Light);
    gfx::Text(t, title, titleFont.Get(), {90, 16, 384, 44}, Theme::Fg);
    gfx::Text(t, hint, hintFont.Get(), {90, 44, 384, 80}, Theme::Dim, gfx::Align::Left, true, true);
    gfx::Text(t, over ? Lang::T(L"Done") : Format(left), countFont.Get(), {24, 84, 376, 140}, Theme::Fg, gfx::Align::Center);

    // Progress: a thin line filling in the accent colour.
    gfx::Fill(t, gfx::Pill(24, 148, 352, 4), Theme::Line);
    float done = over ? 1.0f : 1.0f - left / static_cast<float>(total);
    if (352 * done >= 4) gfx::Fill(t, gfx::Pill(24, 148, 352 * done, 4), Theme::Accent());

    if (!forced && !over) {
        const wchar_t* labels[2] = {L"Snooze 5 min", L"Skip"};
        auto labelFont = gfx::Font(10);
        for (int i = 0; i < 2; i++) {
            const D2D1_RECT_F& b = Buttons[i];
            D2D1_ROUNDED_RECT pill = gfx::Pill(b.left + 0.5f, b.top + 0.5f, b.right - b.left - 1, b.bottom - b.top - 1);
            gfx::Fill(t, pill, down == i ? Theme::ButtonDown : hover == i ? Theme::ButtonHover : Theme::Button);
            gfx::Outline(t, pill, Theme::ButtonEdge);
            gfx::Text(t, Lang::T(labels[i]), labelFont.Get(), b, Theme::Fg, gfx::Align::Center);
        }
    }
    canvas.End();
    canvas.Present(hwnd, at);
}

} // namespace bb
