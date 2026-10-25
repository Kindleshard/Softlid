// Keyboard shortcuts: registering them with Windows, and what each does.
#include "app/App.h"
#include "core/ColorMath.h"
#include "core/Hotkeys.h"
#include "core/Modes.h"
#include <algorithm>

namespace bb {

// (Re)registers every shortcut from the settings. The strip's movement keys only while the strip stays put, so they
// don't take Ctrl+Alt+arrows from other programs the rest of the time.
void App::RegisterHotkeys()
{
    hotkeyFailed = 0;
    stripKeysOn = settings.StripEnabled && !settings.StripFollow;
    for (int id = 1; id <= Hotkeys::Count; id++) {
        UnregisterHotKey(window, id);
        if (Hotkeys::IsStripMove(id) && !stripKeysOn) continue;
        UINT mods, vk;
        if (!Hotkeys::Parse(Hotkeys::Get(settings, id), mods, vk)) continue;
        if (!Hotkeys::Repeats(id)) mods |= MOD_NOREPEAT;
        if (!RegisterHotKey(window, id, mods, vk)) hotkeyFailed |= 1u << id;
    }
}

void App::SuspendHotkeys()
{
    for (int id = 1; id <= Hotkeys::Count; id++) UnregisterHotKey(window, id);
}

bool App::HotkeyFailed(int id) const { return (hotkeyFailed >> id) & 1; }

void App::OnHotkey(int id)
{
    Settings& s = settings;
    switch (id) {
    case Hotkeys::Filter: s.FilterEnabled = !s.FilterEnabled; ManualScreenChange(); break;
    case Hotkeys::Brighter: s.Brightness = std::min(100, s.Brightness + 5); s.FilterEnabled = true; ManualScreenChange(); break;
    case Hotkeys::Dimmer: s.Brightness = std::max(20, s.Brightness - 5); s.FilterEnabled = true; ManualScreenChange(); break;
    case Hotkeys::Warmer:
    case Hotkeys::Cooler:
        s.BlueFilter = id == Hotkeys::Warmer ? std::min(100, s.BlueFilter + 5) : std::max(s.CoolRange ? -75 : 0, s.BlueFilter - 5);
        s.FilterEnabled = true;
        s.ColorEffect = ColorMath::NoEffect;   // a colour effect and the blue light filter take turns
        PreviewNight();
        ManualScreenChange();
        break;
    case Hotkeys::Strip: s.StripEnabled = !s.StripEnabled; break;
    case Hotkeys::Awake: SetKeepAwake(!KeepingAwake()); return;
    case Hotkeys::NextMode: ChooseMode(s.Mode < 0 ? Modes::Health : (s.Mode + 1) % Modes::Count); return;
    case Hotkeys::BreakNow: StartBreak(false); return;
    case Hotkeys::Mini: if (mini) LeaveMiniMode(); else EnterMiniMode(); return;
    case Hotkeys::StripUp: if (strip) strip->Nudge(0, -1); return;
    case Hotkeys::StripDown: if (strip) strip->Nudge(0, 1); return;
    case Hotkeys::StripLeft: if (strip) strip->Nudge(-1, 0); return;
    case Hotkeys::StripRight: if (strip) strip->Nudge(1, 0); return;
    case Hotkeys::StripBigger: ResizeStrip(1); return;
    case Hotkeys::StripSmaller: ResizeStrip(-1); return;
    default: return;
    }
    Changed(true);
    if (settingsWindow) settingsWindow->SyncFromApp();
}

// The strip's size keys: the band's height, or for a vertical band its width; both for a rectangle or circle.
void App::ResizeStrip(int direction)
{
    Settings& s = settings;
    int step = 20 * direction;
    if (s.StripShape == 1) s.StripWidth = std::clamp(s.StripWidth + step, 80, 2000);
    else s.StripHeight = std::clamp(s.StripHeight + step, 40, 400);
    if (s.StripShape == 2) s.StripWidth = std::clamp(s.StripWidth + step * 2, 80, 2000);
    Changed(true);
}

} // namespace bb
