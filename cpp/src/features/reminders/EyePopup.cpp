#include "features/reminders/EyePopup.h"
#include "platform/Activity.h"
#include "ui/EyeArt.h"
#include <algorithm>
#include <cmath>

namespace bb {
namespace {

constexpr UINT_PTR FrameTimer = 1;
constexpr int FrameMs = 30, TotalMs = 1800, FadeMs = 250, BlinkFrom = 500, BlinkTo = 1200;

} // namespace

void EyePopup::Play(const Settings& s, bool instant)
{
    scale = ScaleAtCursor();
    width = static_cast<int>(s.IconSize * scale);
    height = static_cast<int>(width * 0.64f);
    // On the monitor the pointer is on, which is usually the one being looked at.
    RECT wa = WorkAreaAtCursor();
    int m = static_cast<int>(24 * scale);
    switch (s.IconCorner) {
    case 0: at = {wa.left + m, wa.top + m}; break;
    case 1: at = {wa.right - width - m, wa.top + m}; break;
    case 2: at = {wa.left + m, wa.bottom - height - m}; break;
    case 3: at = {wa.right - width - m, wa.bottom - height - m}; break;
    default: at = {wa.left + (wa.right - wa.left - width) / 2, wa.top + (wa.bottom - wa.top - height) / 2}; break;
    }
    opacity = s.IconOpacity / 100.0f;
    fadeScreen = s.BlinkFade && !instant;
    fadeDepth = s.BlinkFadeDepth / 100.0f;
    elapsed = instant ? FadeMs : 0;

    if (!Exists()) Create(L"BlinkBuddy.Eye", WS_POPUP, PopupExStyle | WS_EX_LAYERED | WS_EX_TRANSPARENT, {0, 0, 1, 1});
    Render();
    ShowOnTop(hwnd);
    SetTimer(hwnd, FrameTimer, FrameMs, nullptr);
}

LRESULT EyePopup::OnMessage(UINT message, WPARAM w, LPARAM l)
{
    if (message == WM_TIMER && w == FrameTimer) {
        Step();
        return 0;
    }
    return Window::OnMessage(message, w, l);
}

void EyePopup::Step()
{
    elapsed += FrameMs;
    if (elapsed < TotalMs) {
        Render();
        return;
    }
    KillTimer(hwnd, FrameTimer);
    shade.Hide();
    ShowWindow(hwnd, SW_HIDE);
    canvas.Release();   // nothing to keep until the next blink
}

void EyePopup::Render()
{
    float fade = std::min({1.0f, elapsed / float(FadeMs), (TotalMs - elapsed) / float(FadeMs)});
    float open = 1.0f;
    if (elapsed > BlinkFrom && elapsed < BlinkTo)
        open = static_cast<float>(std::fabs(std::cos(3.14159265 * (elapsed - BlinkFrom) / (BlinkTo - BlinkFrom))));

    // The whole display dims as the eye closes and comes back as it opens, like a real blink.
    if (fadeScreen) {
        float closed = 1 - open;
        if (closed > 0.01f) {
            if (!shade.Showing()) { shade.Show(fadeDepth * closed); ShowOnTop(hwnd); }   // the eye stays above it
            else shade.SetDarkness(fadeDepth * closed);
        } else if (shade.Showing()) {
            shade.Hide();
        }
    }

    if (ID2D1RenderTarget* target = canvas.Begin(width, height, scale)) {
        EyeArt::DrawPopup(target, width / scale, height / scale, open);
        canvas.End();
        canvas.Present(hwnd, at, static_cast<BYTE>(255 * opacity * std::max(0.0f, fade)));
    }
}

} // namespace bb
