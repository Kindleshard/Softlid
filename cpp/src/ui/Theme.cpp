#include "ui/Theme.h"
#include <windows.h>

namespace bb::Theme {
namespace {

int accent = Palette::Accents[0];

// Windows' accent, in the lighter shade Windows itself uses on dark backgrounds; 0 if it can't be read.
int WindowsAccent()
{
    BYTE palette[32] = {};
    DWORD size = sizeof palette;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent", L"AccentPalette",
                     RRF_RT_REG_BINARY, nullptr, palette, &size) == ERROR_SUCCESS && size >= 8)
        return Palette::Argb(palette[4], palette[5], palette[6]);   // "light 2"
    DWORD abgr = 0;
    size = sizeof abgr;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent", L"AccentColorMenu",
                     RRF_RT_REG_DWORD, nullptr, &abgr, &size) == ERROR_SUCCESS)
        return Palette::Argb(abgr & 0xFF, (abgr >> 8) & 0xFF, (abgr >> 16) & 0xFF);
    return 0;
}

} // namespace

int Accent() { return accent; }

void UseAccent(const Settings& s)
{
    int windows = s.AccentFromWindows ? WindowsAccent() : 0;
    accent = windows != 0 ? windows : static_cast<int>(0xFF000000u | static_cast<unsigned>(s.AccentColor));
}

} // namespace bb::Theme
