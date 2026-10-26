#pragma once
#include <windows.h>
#include <string>

namespace bb::Keys {

// Shortcuts as text ("Ctrl+Alt+Shift+F", with Windows Forms key names, as core/Hotkeys keeps them) and as Windows
// key codes.

// Into RegisterHotKey's modifiers (MOD_*) and virtual key; false if it isn't a shortcut.
bool Parse(const std::wstring& text, UINT& mods, UINT& vk);
// A key press as text: modifiers, then the key; "" while only modifiers are held (or none).
std::wstring Describe(UINT vk, bool ctrl, bool alt, bool shift);

} // namespace bb::Keys
