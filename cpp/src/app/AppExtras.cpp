// The app's extras: the focus timer, and mini mode (the settings folded into a small eye that stays on top).
#include "app/App.h"
#include "core/Lang.h"
#include "features/breaks/FocusRest.h"
#include <dwmapi.h>
#include "platform/MemJournal.h"
#include <algorithm>

namespace bb {

// ---- The focus timer (core/FocusTimer keeps the time; the app shows the rest) ----

void App::FocusStart() { focusTimer.Start(); }
void App::FocusPause() { focusTimer.Pause(); }
void App::FocusStop() { focusTimer.Stop(); }
std::wstring App::FocusText() const { return focusTimer.Text(); }

bool App::FocusResting(float& progress, int& secondsLeft) const
{
    if (!restCard || !card) return false;
    auto* rest = static_cast<const FocusRest*>(card.get());   // (the focus timer's rest is always one)
    progress = rest->Progress();
    secondsLeft = rest->SecondsLeft();
    return true;
}

// Once a second while no break shows: at the end of a work period, the rest (a screen of its own).
void App::FocusTick()
{
    if (card || !focusTimer.Tick()) return;
    restCard = true;
    card = std::make_unique<FocusRest>(window, settings, focusTimer.Done());
}

float App::BreakProgress() const
{
    if (Paused()) return -1;
    if (focusTimer.Running()) return focusTimer.Progress();
    if (!settings.BreakEnabled) return -1;
    return reminders.BreakProgress();
}

float App::LookAwayProgress() const
{
    if (Paused() || !settings.BreakEnabled) return -1;
    return reminders.BreakProgress();
}

// ---- Mini mode ----

void App::EnterMiniMode()
{
    if (!settings.MiniEnabled) return;
    MemJournal::Note(L"mini mode");
    settings.MiniMode = true;
    Changed();
    // The settings window folds into the icon (when set so in Options): its picture is taken before it
    // closes, without Windows' own closing fade, and the icon shows once the picture has landed where it will be.
    MiniMorph::Picture picture;
    if (settingsWindow && settings.MiniFold && (!offscreen || CloakedCopy())) {   // (a cloaked diagnostic copy too)
        picture = MiniMorph::Take(settingsWindow->Handle());
        BOOL off = TRUE;
        DwmSetWindowAttribute(settingsWindow->Handle(), DWMWA_TRANSITIONS_FORCEDISABLED, &off, sizeof off);
    }
    if (settingsWindow) settingsWindow->Close();   // (its onClosed tidies up)
    bool fold = !picture.pixels.empty() && !mini;
    if (!mini) mini = std::make_unique<MiniWindow>(*this, fold);
    if (fold) {
        morph = std::make_unique<MiniMorph>(std::move(picture), mini->Bounds(), [this] {
            if (mini) mini->Reveal();
            DeleteLater(std::move(morph));
        });
    }
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
