#pragma once
#include <functional>

namespace bb::VisibilityGuard {

// Keeps the screen readable whatever Brightness and tone add up to: tone may never leave the screen darker than
// Brightness alone does at its lowest setting. That is checked at white and at a dark grey (25%), where dark-mode
// text and panels live. A combination that would go below it gets a higher effective brightness (the slider keeps
// its value); if even full brightness isn't enough, the tone's curve is eased toward straight. Warmth dims both
// sides of the comparison alike, so it is left out.
constexpr double LowestBrightness = 0.2;   // the Brightness slider's minimum

struct Result {
    double brightness = 1;   // effective, 0..1
    double ease = 1;         // 1 = tone as set, down to 0 = no curve
};

// shown(brightness, ease, x): what a grey of x (0..1 as stored) becomes on screen.
using Shown = std::function<double(double brightness, double ease, double x)>;
Result Solve(double brightness, const Shown& shown);

} // namespace bb::VisibilityGuard
