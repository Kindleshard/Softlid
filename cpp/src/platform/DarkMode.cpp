#include "platform/DarkMode.h"
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

namespace bb::DarkMode {

void UseForMenus()
{
    // Windows has no public switch for dark menus; uxtheme's SetPreferredAppMode (exported by number only, since
    // Windows 10 1903) is what Explorer and most apps use. If it's missing, menus just stay light.
    using SetPreferredAppMode = int(WINAPI*)(int mode);
    using FlushMenuThemes = void(WINAPI*)();
    HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!uxtheme) return;
    auto setMode = reinterpret_cast<SetPreferredAppMode>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)));
    auto flush = reinterpret_cast<FlushMenuThemes>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(136)));
    if (setMode) setMode(2);   // ForceDark
    if (flush) flush();
}

void UseForTitleBar(HWND window)
{
    BOOL on = TRUE;
    DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &on, sizeof on);
}

void RoundCorners(HWND window)
{
    DWM_WINDOW_CORNER_PREFERENCE round = DWMWCP_ROUND;
    DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &round, sizeof round);
}

} // namespace bb::DarkMode
