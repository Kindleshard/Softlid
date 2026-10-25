#pragma once
#include <windows.h>
#include <array>
#include <functional>
#include <map>
#include <string>

namespace bb {

// Reshapes brightness at the display output with the graphics card's gamma ramp. Windows refuses a ramp that strays
// more than half the range from normal, so strong curves are split into two stages:
//   1. the colour filter scales the whole image down by Prescale while compositing (no strength limit);
//   2. the gamma ramp, applied at scan-out, lifts darks and mid-tones back where they belong.
// Both are per-frame lookups with nothing to detect, so a white page is tamed on its very first frame.
class GammaRamp {
public:
    GammaRamp() = default;
    ~GammaRamp() { Restore(); }
    GammaRamp(const GammaRamp&) = delete;
    GammaRamp& operator=(const GammaRamp&) = delete;

    void SetCurve(const std::function<double(double)>& f);   // any rising curve with f(0) = 0
    void Off();
    void Verify();          // games, colour profiles or Night Light can reset the ramp: put ours back
    void Reset();           // after a display change the devices may differ
    void ClearLeftover();   // Windows keeps a ramp after its app exits: undo one a crash left behind

    float Prescale() const { return prescale; }
    bool Active() const { return !applied.empty(); }
    bool Refused() const { return refused; }
    const std::wstring& Status() const { return status; }

private:
    using Ramp = std::array<WORD, 768>;
    bool TryApply(double d, const std::function<double(double)>& f);
    const Ramp& Original(const std::wstring& device, HDC dc);
    void Restore();
    std::map<std::wstring, Ramp> originals, applied;
    float prescale = 1;
    bool refused = false;
    std::wstring status;
};

} // namespace bb
