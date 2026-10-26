// ScreenPlan against the code it was taken out of. Old::Decide below is App::ApplyScreen's decision code as it was
// before the split (commit 32b94bf), kept word for word apart from reading the engines' state from arguments. Every
// combination of the settings and states that matter must decide exactly the same.
#include "Check.h"
#include "core/ColorMath.h"
#include "core/Lang.h"
#include "core/ScreenPlan.h"
#include "core/ToneCurve.h"
#include "core/VisibilityGuard.h"
#include <cstdio>
#include <functional>

using namespace bb;

namespace Old {

struct Inputs {
    bool labCompare, smartFailed, layerFailed, lutFailed, effectsAvailable;
    double white;          // comfort.White()
    float tonePrescale;    // tone.Prescale(), after the curve was set
    bool toneRefused;      // tone.Refused()
};

struct Result {
    bool useSmart, useLayer, useLut, custom, preset, effectShown, wantsLut;
    VisibilityGuard::Result guard;
    std::wstring guardNote, key;
    std::function<double(double)> curve;   // what tone.SetCurve got (empty: tone.Off())
    float prescale, onScreen;
};

Result Decide(const Settings& s, const Inputs& in)
{
    Result r{};
    bool labCompare = in.labCompare;
    bool on = s.FilterEnabled;
    int engine = s.ToneMode;
    bool toneOn = on && s.ToneEnabled && !(labCompare && engine != 4);
    bool useSmart = toneOn && engine == 2 && !in.smartFailed;
    r.effectShown = on && s.ColorEffect != ColorMath::NoEffect && !useSmart && in.effectsAvailable;
    bool useLayer = toneOn && engine == 3 && !in.layerFailed;
    r.wantsLut = toneOn && engine == 4;
    bool useLut = toneOn && engine == 4 && !in.lutFailed;
    bool custom = toneOn && s.CustomTone() && (s.ShadowCrush > 0 || s.Squeeze > 0 || s.LimiterStrength > 0);
    bool preset = toneOn && !s.CustomTone() && !useSmart && !useLayer && !useLut;

    double gamma = ToneCurve::Gamma(s.Level()), white = in.white;
    int lvl = s.Level();
    std::function<double(double, double, double)> shown;
    if (preset) shown = [=](double b, double e, double x) { return b * white * std::pow(x, 1 + (gamma - 1) * e); };
    else if (custom)
        shown = [&s](double b, double e, double x) {
            return b * ToneCurve::Net(x, static_cast<int>(s.ShadowCrush * e), static_cast<int>(s.Squeeze * e), static_cast<int>(s.LimiterStrength * e));
        };
    else if (useSmart || useLayer) shown = [lvl](double b, double, double x) { return b * ToneCurve::Flat(lvl, x); };
    else if (useLut && !labCompare) shown = [lvl](double b, double, double x) { return ToneCurve::Flat(lvl, b * x); };
    else shown = [](double b, double, double x) { return b * x; };
    VisibilityGuard::Result guard;
    if (on) guard = VisibilityGuard::Solve(s.Brightness / 100.0, shown);
    if (guard.ease < 1)
        r.guardNote = Lang::T(L"Visibility guard: brightness at 100% and the curve eased so dark details stay visible.");
    else if (guard.brightness > s.Brightness / 100.0 + 0.005)
        r.guardNote = Lang::F(L"Visibility guard: brightness held at {0:0}% (set to {1}%) so the screen stays readable.",
                              {std::to_wstring(std::lround(guard.brightness * 100)), std::to_wstring(s.Brightness)});

    float bright = static_cast<float>(guard.brightness);
    double g = 1 + (gamma - 1) * guard.ease;
    int sh = static_cast<int>(s.ShadowCrush * guard.ease), sq = static_cast<int>(s.Squeeze * guard.ease),
        li = static_cast<int>(s.LimiterStrength * guard.ease);
    wchar_t key[64] = L"off";
    if (preset) swprintf_s(key, L"preset%.3f", g);
    else if (custom) swprintf_s(key, L"%d,%d,%d@%.2f", sh, sq, li, bright);
    if (preset) r.curve = [g](double x) { return std::pow(x, g); };
    else if (custom) r.curve = [=](double x) { return bright * ToneCurve::Net(x, sh, sq, li); };
    float prescale = in.tonePrescale, onScreen = bright;
    if (preset) {
        prescale *= static_cast<float>(std::pow(bright * white, 1 / g));
        onScreen = 1;
    } else if (custom && !in.toneRefused) onScreen = 1;

    r.useSmart = useSmart;
    r.useLayer = useLayer;
    r.useLut = useLut;
    r.custom = custom;
    r.preset = preset;
    r.guard = guard;
    r.key = key;
    r.prescale = prescale;
    r.onScreen = onScreen;
    return r;
}

} // namespace Old

TEST(ScreenPlanDecidesAsBefore)
{
    Lang::Use(L"en");
    const int sliders[][3] = {{0, 0, 0}, {30, 0, 0}, {0, 50, 60}, {100, 100, 100}};
    long cases = 0, mismatches = 0;
    auto mismatch = [&](const char* what, const Settings& s) {
        if (++mismatches <= 5)
            std::printf("    mismatch in %s: filter %d tone %d engine %d level %d sliders %d/%d/%d brightness %d effect %d\n", what,
                        s.FilterEnabled, s.ToneEnabled, s.ToneMode, s.ComfortLevel, s.ShadowCrush, s.Squeeze, s.LimiterStrength,
                        s.Brightness, s.ColorEffect);
    };
    for (int filter = 0; filter <= 1; filter++)
    for (int tone = 0; tone <= 1; tone++)
    for (int engine : {0, 2, 3, 4})
    for (int level = 0; level <= 4; level++)
    for (const auto& sl : sliders)
    for (int brightness : {20, 45, 100})
    for (int effect : {0, 2})
    for (int lab = 0; lab <= 1; lab++)
    for (int failures = 0; failures < 8; failures++)
    for (int effects = 0; effects <= 1; effects++) {
        Settings s;
        s.FilterEnabled = filter != 0;
        s.ToneEnabled = tone != 0;
        s.ToneMode = engine;
        s.ComfortLevel = level;
        s.ShadowCrush = sl[0];
        s.Squeeze = sl[1];
        s.LimiterStrength = sl[2];
        s.Brightness = brightness;
        s.ColorEffect = effect;
        double white = ToneCurve::White(s.Level()) * (level % 2 ? 0.9 : 1.0);   // (adapting to a white screen, or not)
        ScreenPlan::State state{lab != 0, (failures & 1) != 0, (failures & 2) != 0, (failures & 4) != 0, effects != 0};
        ScreenPlan::Engines e = ScreenPlan::Choose(s, state);
        VisibilityGuard::Result guard = ScreenPlan::Guard(s, e, white, state.labCompare);
        ScreenPlan::Ramp ramp = ScreenPlan::RampFor(s, e, guard);
        for (float tonePrescale : {1.0f, 0.8f})
        for (int refused = 0; refused <= 1; refused++) {
            cases++;
            Old::Result old = Old::Decide(s, {state.labCompare, state.smartFailed, state.layerFailed, state.lutFailed,
                                              state.effectsAvailable, white, tonePrescale, refused != 0});
            if (old.useSmart != e.smart || old.useLayer != e.layer || old.useLut != e.lut || old.custom != e.custom ||
                old.preset != e.preset || old.effectShown != e.effectShown || old.wantsLut != ScreenPlan::WantsLut(s, state.labCompare))
                mismatch("engines", s);
            if (old.guard.brightness != guard.brightness || old.guard.ease != guard.ease) mismatch("guard", s);
            if (old.guardNote != ScreenPlan::GuardNote(guard, s.Brightness)) mismatch("guard note", s);
            if (old.key != ramp.key) mismatch("ramp key", s);
            if (static_cast<bool>(old.curve) != (ramp.kind != ScreenPlan::Ramp::Kind::None)) mismatch("ramp on/off", s);
            if (old.curve)
                for (double x : {0.0, 0.1, 0.37, 0.5, 0.82, 1.0})
                    if (old.curve(x) != ramp(x)) mismatch("ramp curve", s);
            ScreenPlan::Gains gains = ScreenPlan::FilterGains(e, ramp, tonePrescale, refused != 0, white);
            if (old.prescale != gains.prescale || old.onScreen != gains.onScreen) mismatch("filter gains", s);
        }
    }
    CHECK_EQ(cases, 245760L);
    CHECK_EQ(mismatches, 0L);
}
