#include "core/ScreenPlan.h"
#include "core/ColorMath.h"
#include "core/Lang.h"
#include "core/ToneCurve.h"
#include <cmath>
#include <cstdio>

namespace bb::ScreenPlan {

namespace {


// The lab's compare button shows the screen without tone; DesktopLUT gets a do-nothing table instead of stopping.
bool ToneOn(const Settings& s, bool labCompare) { return s.FilterEnabled && s.ToneEnabled && !(labCompare && s.ToneMode != Settings::ToneDesktopLut); }

} // namespace

bool WantsLut(const Settings& s, bool labCompare) { return ToneOn(s, labCompare) && s.ToneMode == Settings::ToneDesktopLut; }

Engines Choose(const Settings& s, const State& state)
{
    Engines e;
    e.on = s.FilterEnabled;
    e.toneOn = ToneOn(s, state.labCompare);
    e.smart = e.toneOn && s.ToneMode == Settings::ToneSmartGpu && !state.smartFailed;
    // A colour effect pauses the blue light filter, where one can show (not in the GPU filter or the overlays).
    e.effectShown = e.on && s.ColorEffect != ColorMath::NoEffect && !e.smart && state.effectsAvailable;
    e.layer = e.toneOn && s.ToneMode == Settings::ToneSmartLayer && !state.layerFailed;
    e.lut = e.toneOn && s.ToneMode == Settings::ToneDesktopLut && !state.lutFailed;
    e.custom = e.toneOn && s.CustomTone() && (s.ShadowCrush > 0 || s.Squeeze > 0 || s.LimiterStrength > 0);
    e.preset = e.toneOn && !s.CustomTone() && !e.smart && !e.layer && !e.lut;
    return e;
}

VisibilityGuard::Shown Shown(const Settings& s, const Engines& e, double comfortWhite, bool labCompare)
{
    double gamma = ToneCurve::Gamma(s.Level()), white = comfortWhite;
    int level = s.Level(), shadows = s.ShadowCrush, squeeze = s.Squeeze, limit = s.LimiterStrength;
    if (e.preset) return [=](double b, double ease, double x) { return b * white * std::pow(x, 1 + (gamma - 1) * ease); };
    if (e.custom)
        return [=](double b, double ease, double x) {
            return b * ToneCurve::Net(x, static_cast<int>(shadows * ease), static_cast<int>(squeeze * ease), static_cast<int>(limit * ease));
        };
    if (e.smart || e.layer) return [level](double b, double, double x) { return b * ToneCurve::Flat(level, x); };
    if (e.lut && !labCompare) return [level](double b, double, double x) { return ToneCurve::Flat(level, b * x); };   // the table comes after on-screen dimming
    return [](double b, double, double x) { return b * x; };
}

VisibilityGuard::Result Guard(const Settings& s, const Engines& e, double comfortWhite, bool labCompare)
{
    if (!e.on) return {};
    return VisibilityGuard::Solve(s.Brightness / 100.0, Shown(s, e, comfortWhite, labCompare));
}

std::wstring GuardNote(const VisibilityGuard::Result& guard, int brightness)
{
    if (guard.ease < 1) return Lang::T(L"Visibility guard: brightness at 100% and the curve eased so dark details stay visible.");
    if (guard.brightness > brightness / 100.0 + 0.005)
        return Lang::F(L"Visibility guard: brightness held at {0:0}% (set to {1}%) so the screen stays readable.",
                       {std::to_wstring(std::lround(guard.brightness * 100)), std::to_wstring(brightness)});
    return L"";
}

double Ramp::operator()(double x) const
{
    if (kind == Kind::Preset) return std::pow(x, gamma);
    if (kind == Kind::Custom) return bright * ToneCurve::Net(x, shadows, squeeze, limit);
    return x;
}

Ramp RampFor(const Settings& s, const Engines& e, const VisibilityGuard::Result& guard)
{
    Ramp r;
    r.bright = static_cast<float>(guard.brightness);
    r.gamma = 1 + (ToneCurve::Gamma(s.Level()) - 1) * guard.ease;
    r.shadows = static_cast<int>(s.ShadowCrush * guard.ease);
    r.squeeze = static_cast<int>(s.Squeeze * guard.ease);
    r.limit = static_cast<int>(s.LimiterStrength * guard.ease);
    wchar_t key[64] = L"off";
    if (e.preset) {
        r.kind = Ramp::Kind::Preset;
        swprintf_s(key, L"preset%.3f", r.gamma);
    } else if (e.custom) {
        r.kind = Ramp::Kind::Custom;
        swprintf_s(key, L"%d,%d,%d@%.2f", r.shadows, r.squeeze, r.limit, r.bright);
    }
    r.key = key;
    return r;
}

Gains FilterGains(const Engines& e, const Ramp& ramp, float rampPrescale, bool rampRefused, double comfortWhite)
{
    Gains g{rampPrescale, ramp.bright};
    if (e.preset) {
        g.prescale *= static_cast<float>(std::pow(ramp.bright * comfortWhite, 1 / ramp.gamma));
        g.onScreen = 1;
    } else if (e.custom && !rampRefused) {
        g.onScreen = 1;
    }
    return g;
}

} // namespace bb::ScreenPlan
