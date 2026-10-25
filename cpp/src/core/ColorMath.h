#pragma once
#include <array>

namespace bb::ColorMath {

// The colour maths of the screen filter, with no windows: warmth from a colour temperature, the colour effects, and
// the 5x5 matrix Windows' colour effect takes.

// The blue light filter: 0% is daylight (6500 K), 100% candle-like (1900 K); below 0, cooler (up to ~10000 K).
double Kelvin(double percent);
int KelvinFor(double percent);   // rounded to 50 K, for showing

struct Rgb { float r = 1, g = 1, b = 1; };
Rgb KelvinToRgb(double kelvin);   // channel gains, 6500 K being exactly neutral

// The colour effects: 0 none, 1 grayscale (style 0 Rec.709, 1 Rec.601, 2 black and white), 2 sepia, 3 e-ink (soft
// grey on warm paper), 4 red night (everything in shades of red, for the dark).
enum Effect { NoEffect = 0, Grayscale = 1, Sepia = 2, Eink = 3, RedNight = 4 };

// Row-vector matrix (out = [r g b a 1] x M, so row 4 holds the offsets): the effect first, then the per-channel
// gains (brightness and warmth), then inversion.
using Matrix = std::array<float, 25>;
Matrix Build(Rgb gains, bool invert, int effect, int style);

// What a colour (0..255 each) looks like through an effect, for the previews.
void Through(int effect, int style, int& r, int& g, int& b);

} // namespace bb::ColorMath
