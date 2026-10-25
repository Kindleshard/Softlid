#include "core/ToneCurve.h"
#include <algorithm>
#include <cmath>

namespace bb::ToneCurve {
namespace {

constexpr double Gammas[] = {1.15, 1.3, 1.45, 1.45};   // Max dims through its white level: a steeper curve would bury dark grey
constexpr double Whites[] = {0.85, 0.7, 0.55, 0.4};
// Smart: how much bright backgrounds move toward mid-grey (1 = not at all), how much dark ones move away from it, and
// extra overall dimming, all in stops. A white page ends up at about 80, 68, 58 and 40% (as stored).
constexpr float Compress[] = {0.8f, 0.65f, 0.5f, 0.35f}, Expand[] = {1.05f, 1.15f, 1.25f, 1.3f}, Dim[] = {-0.2f, -0.35f, -0.5f, -1.3f};
constexpr double Anchor = -2.47;   // log2 of mid-grey (18%)

double Smooth(double t) { return t <= 0 ? 0 : t >= 1 ? 1 : t * t * (3 - 2 * t); }

// Darks get darker and bunch up toward black: a power curve that blends back to normal by mid-grey.
double Shadows(double x, double s)
{
    if (s <= 0) return x;
    double w = Smooth(x / 0.5);
    return std::pow(x, 1 + 1.5 * s) * (1 - w) + x * w;
}

// Everything above a quarter brightness is compressed toward it (down to 15% of its spread), easing in smoothly.
double Squeeze(double x, double s)
{
    if (s <= 0) return x;
    constexpr double anchor = 0.25, ease = 0.2;
    double u = x - anchor, excess;
    if (u <= 0) excess = 0;
    else if (u < ease) {
        double t = u / ease;
        excess = ease * (t * t * t - t * t * t * t / 2);   // the integral of smoothstep
    } else {
        excess = u - ease / 2;
    }
    return x - 0.85 * s * excess;
}

// Untouched up to a knee, then an exponential shoulder that leaves the knee at slope ~1 and reaches the cap at full input.
double Limit(double x, double cap)
{
    if (cap >= 1) return x;
    double knee = cap >= 0.5 ? (3 * cap - 1) / 2 : cap / 2, span = cap - knee;
    if (x <= knee) return x;
    return knee + span * (1 - std::exp(-(x - knee) / span)) / (1 - std::exp(-(1 - knee) / span));
}

} // namespace

const wchar_t* const Levels[4] = {L"Gentle", L"Balanced", L"Strong", L"Max"};

double Gamma(int level) { return Gammas[std::clamp(level, 0, 3)]; }
double White(int level) { return Whites[std::clamp(level, 0, 3)]; }

double CapFor(int limit) { return 1 - 0.8 * limit / 100.0; }

double Net(double x, int shadows, int squeeze, int limit)
{
    return Limit(Squeeze(Shadows(x, shadows / 100.0), squeeze / 100.0), CapFor(limit));
}

double Remap(int level, double b)
{
    int l = std::clamp(level, 0, 3);
    double nb = (b > Anchor ? Anchor + (b - Anchor) * Compress[l] : Anchor + (b - Anchor) * Expand[l]) + Dim[l];
    return std::min(nb, b);
}

double Flat(int level, double x)
{
    if (x <= 0) return 0;
    double b = std::log2(std::pow(x, 2.2));
    return std::pow(std::pow(2.0, Remap(level, b)), 1 / 2.2);
}

SmartParams ForLevel(int level)
{
    int l = std::clamp(level, 0, 3);
    return {Compress[l], Expand[l], Dim[l]};
}

void Map(double r, double g, double b, int level, double& outR, double& outG, double& outB)
{
    if (level < 0) {
        outR = r;
        outG = g;
        outB = b;
        return;
    }
    double y = 0.2126 * std::pow(r, 2.2) + 0.7152 * std::pow(g, 2.2) + 0.0722 * std::pow(b, 2.2);
    if (y <= 1e-7) {
        outR = outG = outB = 0;
        return;
    }
    double l = std::log2(y);
    double k = std::pow(std::pow(2.0, Remap(level, l) - l), 1 / 2.2);   // in linear light, at most 1
    outR = r * k;
    outG = g * k;
    outB = b * k;
}

// Above d (only reached by things the colour effect doesn't touch, such as some cursors) it keeps climbing with slope 1.
double Lookup(double y, double d, const std::function<double(double)>& f) { return y <= d ? f(y / d) : std::min(1.0, f(1) + (y - d)); }

} // namespace bb::ToneCurve
