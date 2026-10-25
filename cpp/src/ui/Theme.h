#pragma once
#include "core/Palette.h"
#include "core/Settings.h"

namespace bb::Theme {

// BlinkBuddy's dark colours (ARGB), the same as the C# version.
inline constexpr int Bg = Palette::Argb(32, 32, 32);
inline constexpr int Card = Palette::Argb(40, 40, 42);
inline constexpr int Line = Palette::Argb(58, 58, 58);
inline constexpr int Fg = Palette::Argb(242, 242, 242);
inline constexpr int Dim = Palette::Argb(160, 160, 160);
inline constexpr int Track = Palette::Argb(105, 105, 105);
inline constexpr int Button = Palette::Argb(55, 55, 55);
inline constexpr int ButtonHover = Palette::Argb(66, 66, 66);
inline constexpr int ButtonDown = Palette::Argb(46, 46, 46);
inline constexpr int ButtonEdge = Palette::Argb(75, 75, 75);
inline constexpr int Good = Palette::Argb(108, 203, 95);
inline constexpr int Warn = Palette::Argb(255, 163, 64);
inline constexpr int Bad = Palette::Argb(255, 105, 97);

// The accent: Windows' own (when asked, and readable) or the one picked in Options.
int Accent();
void UseAccent(const Settings& s);

} // namespace bb::Theme
