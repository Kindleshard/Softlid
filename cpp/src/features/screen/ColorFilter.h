#pragma once
#include "core/Settings.h"
#include <memory>
#include <vector>

namespace bb {

// Dimming, warmth, colour effects and inversion for the whole screen. Prefers Windows' full-screen colour effect (no
// windows and practically no CPU or GPU cost); if Windows refuses it, falls back to see-through overlay windows,
// which can only dim and warm.
class ColorFilter {
public:
    ColorFilter();
    ~ColorFilter();
    ColorFilter(const ColorFilter&) = delete;
    ColorFilter& operator=(const ColorFilter&) = delete;

    // night: how far into the night schedule it is (0 day .. 1 night). prescale and brightness: the on-screen
    // dimming (1 when the tone curve or the backlight does it).
    void Apply(const Settings& s, float night, float prescale, float brightness);
    void Off();   // back to a plain screen (also after unlocking or a display change, when Windows may have reset it)

    bool CanShowEffects() const { return !magFailed; }   // the overlays can't do the effects

    // Per-channel brightness and warmth gains, for the Smart (GPU) engine to apply in its shader instead (bright: the
    // on-screen brightness, which the visibility guard may raise above the setting).
    static void Scale(const Settings& s, float night, float bright, float& r, float& g, float& b);

private:
    void StopMagnifier();
    void ApplyOverlays(bool neutral, float bright, float warmth);

    bool magOn = false, magFailed = false;
    class Overlay;
    std::vector<std::unique_ptr<Overlay>> overlays;
};

} // namespace bb
