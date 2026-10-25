#pragma once
#include <windows.h>

namespace bb::DarkMode {

// BlinkBuddy is always dark: menus (the tray menu included) and window title bars.
void UseForMenus();
void UseForTitleBar(HWND window);
void RoundCorners(HWND window);

} // namespace bb::DarkMode
