#pragma once
#include <cstdint>

namespace bb {

// The app's fixed colours, as ARGB in an int (the way settings store colours).
namespace Palette {

constexpr int Argb(int r, int g, int b) { return static_cast<int>(0xFF000000u | (unsigned(r) << 16) | (unsigned(g) << 8) | unsigned(b)); }

// Accent choices (the first is BlinkBuddy's blue).
inline constexpr int Accents[] = {
    Argb(76, 194, 255), Argb(64, 214, 196), Argb(120, 210, 110), Argb(240, 200, 70),
    Argb(255, 163, 64), Argb(255, 112, 112), Argb(255, 128, 170), Argb(176, 150, 255),
};

// Reading strip tints: black, night blue, sepia brown, forest, plum, slate.
inline constexpr int StripTints[] = {
    Argb(0, 0, 0), Argb(10, 18, 44), Argb(46, 30, 14), Argb(12, 34, 20), Argb(38, 14, 40), Argb(30, 36, 44),
};

// The mini icon's glow (index 0 meant "the accent", so it isn't a colour here).
inline constexpr int Glows[] = {
    0, Argb(64, 214, 196), Argb(150, 226, 120), Argb(255, 190, 90), Argb(255, 128, 170), Argb(232, 232, 240),
};

// Each screen mode's own soft colour (Pause, Health, Game, Movie, Office, Editing, Reading, Custom), for its icon.
inline constexpr int ModeTints[] = {
    Argb(170, 176, 190), Argb(244, 143, 177), Argb(179, 157, 250), Argb(255, 171, 145),
    Argb(129, 199, 245), Argb(128, 222, 190), Argb(255, 213, 128), Argb(190, 196, 214),
};

} // namespace Palette
} // namespace bb
