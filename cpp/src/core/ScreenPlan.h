#pragma once
#include "core/Settings.h"
#include "core/VisibilityGuard.h"
#include <string>

namespace bb::ScreenPlan {

// What the screen pipeline decides before anything is shown: which tone engine does the work, what a grey becomes
// on screen (for the visibility guard), the gamma ramp, and the colour filter's gains. Pure decisions: App's
// ApplyScreen carries them out with the engines.

// Whether DesktopLUT is wanted. It has to be started before the rest can be decided: starting it may fail at once.
bool WantsLut(const Settings& s, bool labCompare);

// The engines' state as the decisions need it.
struct State {
    bool labCompare = false;       // the tone lab's "without tone" button is held
    bool smartFailed = false, layerFailed = false, lutFailed = false;
    bool effectsAvailable = true;  // the colour filter can show effects (not the overlay fallback)
};

struct Engines {
    bool on = false;           // the screen filter switch
    bool toneOn = false;       // tone is on (and the lab isn't comparing)
    bool smart = false;        // Smart (GPU): tone, brightness, warmth and invert in its shader
    bool layer = false;        // Smart layer: a dimming layer does the tone
    bool lut = false;          // DesktopLUT: the colour table in the compositor does the tone
    bool preset = false;       // Standard at a preset strength (also where a failed experimental engine falls back)
    bool custom = false;       // Standard's custom curve, with at least one slider set
    bool effectShown = false;  // a colour effect is taking the blue light filter's place
};

// Tone has its own switch; the screen filter switch turns everything off, tone included. An experimental engine
// that failed falls back to the Standard preset at the same strength.
Engines Choose(const Settings& s, const State& state);

// What a grey (x) becomes on screen for a brightness and an easing of the curve, per engine. (The real backlight is
// separate; Brightness always dims pixels.) comfortWhite: where Standard's white is right now.
VisibilityGuard::Shown Shown(const Settings& s, const Engines& e, double comfortWhite, bool labCompare);
VisibilityGuard::Result Guard(const Settings& s, const Engines& e, double comfortWhite, bool labCompare);
std::wstring GuardNote(const VisibilityGuard::Result& guard, int brightness);   // "" while it isn't stepping in

// The gamma ramp: a preset's power curve, the custom curve (which carries the on-screen dimming itself), or none.
struct Ramp {
    enum class Kind { None, Preset, Custom } kind = Kind::None;
    double gamma = 1;                          // Preset, eased by the guard
    int shadows = 0, squeeze = 0, limit = 0;   // Custom, eased by the guard
    float bright = 1;                          // the on-screen dimming
    std::wstring key = L"off";                 // changes exactly when the curve does
    double operator()(double x) const;
};
Ramp RampFor(const Settings& s, const Engines& e, const VisibilityGuard::Result& guard);

// The colour filter's gains. Brightness belongs after the curve: the filter runs before the gamma ramp, so the
// presets feed it brightness^(1/gamma), and the custom curve carries brightness itself; otherwise the curve would
// multiply the dimming (20% brightness at Max would leave white at 3%). rampPrescale and rampRefused: what the gamma
// ramp asks of the filter, and whether Windows refused the ramp.
struct Gains {
    float prescale = 1, onScreen = 1;
};
Gains FilterGains(const Engines& e, const Ramp& ramp, float rampPrescale, bool rampRefused, double comfortWhite);

// Static grain rides in the Smart layer, so there is one layer over the screen instead of two.
inline bool BakesGrain(const Settings& s, const Engines& e) { return e.layer && s.GrainEnabled && !s.GrainAnimated; }

} // namespace bb::ScreenPlan
