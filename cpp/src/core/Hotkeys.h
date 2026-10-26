#pragma once
#include "core/Settings.h"
#include <string>

namespace bb::Hotkeys {

// The keyboard shortcuts: each action, its default keys, and the keys you set instead (kept in Settings::Hotkeys as
// "id=Ctrl+Alt+Shift+F;..."; an action set to nothing has no shortcut). Keys are written as Windows Forms names
// ("Up", "Oemplus"), the same as the C# version. The reading strip's movement keys only work while the strip stays
// put (not following the mouse).
enum Id {
    Filter = 1, Brighter, Dimmer, Warmer, Cooler, Strip, Awake, NextMode, BreakNow, Mini,
    StripUp, StripDown, StripLeft, StripRight, StripBigger, StripSmaller,
    Count = StripSmaller
};

extern const wchar_t* const Names[Count];   // English, by id - 1

bool Repeats(int id);                       // held keys repeat for these; the switches fire once per press
inline bool IsStripMove(int id) { return id >= StripUp; }

std::wstring Get(const Settings& s, int id);   // "" when the action has none
void Set(Settings& s, int id, const std::wstring& keys);
inline void ResetAll(Settings& s) { s.Hotkeys.clear(); }

// (Turning keys into Windows key codes and back is in platform/Keys.)
// For showing: arrows and the plus/minus keys as symbols ("Ctrl + Alt + ↑").
std::wstring Pretty(const std::wstring& keys);

} // namespace bb::Hotkeys
