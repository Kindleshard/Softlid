#pragma once
#include "core/Focus.h"
#include "core/Settings.h"
#include <windows.h>
#include <string>

namespace bb {

// What mini mode needs from the app (App implements it): the settings, the screen's state to draw, the focus timer
// and the next break, and the way back to the settings.
class MiniHost {
public:
    virtual Settings& S() = 0;
    virtual void Changed(bool screen = false) = 0;   // settings changed: saved soon; screen: shown at once
    virtual void ManualScreenChange() = 0;            // a slider moved: your own values again, not a preset
    virtual void PreviewNight() = 0;                  // the blue light slider moves: show the daytime strength
    virtual bool EffectsAvailable() const = 0;
    virtual float WarmthNow() const = 0;              // the blue light filter in effect now (0..1)

    virtual Focus FocusState() const = 0;
    virtual std::wstring FocusText() const = 0;
    virtual void FocusStart() = 0;
    virtual void FocusPause() = 0;
    virtual void FocusStop() = 0;
    // The focus timer's rest, while it shows: how far into it (0..1) and the seconds left; false when not resting.
    virtual bool FocusResting(float& progress, int& secondsLeft) const = 0;
    virtual std::wstring BreakStatus() const = 0;
    virtual float BreakProgress() const = 0;          // the wait for the next break (0..1); -1: none
    virtual float LookAwayProgress() const = 0;       // the wait for the next look-away break alone (0..1); -1: none

    virtual void LeaveMiniMode() = 0;                 // and the settings open
    virtual void HideMini() = 0;                      // Minimize: the icon goes, Softlid stays in the tray
    virtual void SaveMiniPosition(POINT at) = 0;
    // The tray icon's menu, at the pointer (Settings, pauses, Exit...); fromMini: with Resize, for the icon.
    virtual void ShowMenu(bool fromMini) = 0;

protected:
    ~MiniHost() = default;
};

} // namespace bb
