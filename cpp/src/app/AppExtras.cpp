// The app's extras: the focus timer, and mini mode (the settings folded into a small eye that stays on top).
#include "app/App.h"
#include "core/Lang.h"
#include "platform/MemJournal.h"
#include <algorithm>

namespace bb {

// ---- The focus timer (core/FocusTimer keeps the time; the app shows the rest) ----

void App::FocusStart() { focusTimer.Start(); }
void App::FocusPause() { focusTimer.Pause(); }
void App::FocusStop() { focusTimer.Stop(); }
std::wstring App::FocusText() const { return focusTimer.Text(); }

// Once a second while no break shows: at the end of a work period, the rest card.
void App::FocusTick()
{
    if (card || !focusTimer.Tick()) return;
    restCard = true;
    card = std::make_unique<BreakCard>(window, true, settings, settings.FocusRest * 60, Lang::T(L"Time to rest"),
                                       Lang::T(L"Stand up, look away, stretch. The next focus period starts after this."));
}

float App::BreakProgress() const
{
    if (Paused()) return -1;
    if (focusTimer.Running()) return focusTimer.Progress();
    if (!settings.BreakEnabled) return -1;
    return reminders.BreakProgress();
}

// ---- Mini mode ----

void App::EnterMiniMode()
{
    if (!settings.MiniEnabled) return;
    MemJournal::Note(L"mini mode");
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
