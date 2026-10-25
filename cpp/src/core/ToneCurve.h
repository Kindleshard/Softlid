#pragma once
#include <functional>

namespace bb::ToneCurve {

// The tone maths, with no windows: what each engine does to brightness, shared by the engines, the settings graph and
// the visibility guard.

// The four strengths every engine shares; a level puts white at about the same place in each.
extern const wchar_t* const Levels[4];   // English: Gentle, Balanced, Strong, Max

// Standard (the gamma curve): the curve's power and where pure white ends up, for a level.
double Gamma(int level);
double White(int level);

// Custom (the three sliders, Standard only): input brightness 0..1 -> what is shown.
double CapFor(int limit);   // 0% = no cap, 100% = whites at 20%
double Net(double x, int shadows, int squeeze, int limit);

// The smart engines: the new log2 brightness for a background of log2 brightness b (bright toward mid-grey, dark a
// little deeper, then dimmed; never brighter), and what a large flat area of brightness x (0..1, as stored) becomes.
double Remap(int level, double b);
double Flat(int level, double x);
struct SmartParams { float compress, expand, dimStops; };
SmartParams ForLevel(int level);

// One colour (0..1 each, as the display stores them) through the smart curve applied to whole colours, as DesktopLUT's
// table does: one factor on all three channels, so hue is kept and nothing clips. Level -1 changes nothing.
void Map(double r, double g, double b, int level, double& outR, double& outG, double& outB);

// The gamma ramp's lookup for a pre-scale d: undo the pre-scale, then apply the curve f.
double Lookup(double y, double d, const std::function<double(double)>& f);

} // namespace bb::ToneCurve
