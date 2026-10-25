// The app's extras: the focus timer, and mini mode (the settings folded into a small eye that stays on top).
#include "app/App.h"
#include "core/Lang.h"
#include <algorithm>

namespace bb {
namespace {

std::wstring MinSec(int seconds)
{
    seconds = std::max(0, seconds);
    wchar_t text[16];
    swprintf_s(text, L"%d:%02d", seconds / 60, seconds % 60);
    return text;
}

} // namespace

// ---- The focus timer ----

void App::FocusStart()
{
    if (focus == Focus::Paused) {
        focus = Focus::Working;
        return;
    }
    focus = Focus::Working;
    focusLeft = settings.FocusWork * 60;
}

void App::FocusPause()
{
    if (focus == Focus::Working) focus = Focus::Paused;
}

void App::FocusStop()
{
    focus = Focus::Off;
    focusLeft = 0;
}

// Once a second while it works: at the end of a period, the rest card (the look-away and stand-up reminders wait
// while the timer runs; it decides when to rest instead).
void App::FocusTick()
{
    if (focus != Focus::Working || card) return;
    if (--focusLeft > 0) return;
    focusDone++;
    focus = Focus::Resting;
    restCard = true;
    card = std::make_unique<BreakCard>(window, true, settings, settings.FocusRest * 60, Lang::T(L"Time to rest"),
                                       Lang::T(L"Stand up, look away, stretch. The next focus period starts after this."));
}

std::wstring App::FocusText() const
{
    if (focus == Focus::Off) return MinSec(settings.FocusWork * 60);
    if (focus == Focus::Resting) return Lang::T(L"Resting");
    return MinSec(focusLeft);
}

float App::BreakProgress() const
{
    if (Paused()) return -1;
    if (focus == Focus::Working || focus == Focus::Paused) return 1 - focusLeft / static_cast<float>(std::max(1, settings.FocusWork * 60));
    if (!settings.BreakEnabled) return -1;
    return reminders.BreakProgress();
}

// ---- Mini mode ----

void App::EnterMiniMode()
{
    if (!settings.MiniEnabled) return;
    settings.MiniMode = true;
    Changed();
    if (settingsWindow) settingsWindow->Close();   // (its onClosed tidies up)
    ShowMini();
}

void App::ShowMini()
{
    if (!mini) mini = std::make_unique<MiniWindow>(*this);
}

void App::LeaveMiniMode()
{
    settings.MiniMode = false;
    Changed();
    PostMessageW(window, LeaveMiniMessage, 0, 0);   // (not from inside the eye's own click: it goes away)
}

void App::SaveMiniPosition(POINT at)
{
    settings.MiniX = at.x;
    settings.MiniY = at.y;
    Changed();
}

} // namespace bb
