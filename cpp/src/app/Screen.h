#pragma once
#include "core/ScreenPlan.h"
#include "core/Settings.h"
#include "features/screen/ColorFilter.h"
#include "features/settings/SettingsHost.h"
#include "features/tone/Comfort.h"
#include "features/tone/GammaRamp.h"
#include "features/tone/Grain.h"
#include "features/tone/LutGuard.h"
#include "features/tone/SmartFilter.h"
#include "features/tone/SmartLayer.h"
#include "platform/NightLight.h"
#include <windows.h>
#include <chrono>
#include <functional>
#include <optional>
#include <string>

namespace bb {

// The screen pipeline: from the settings (with the night schedule, a rule's mode and Windows Night light) to the
// colour filter and the tone engines. ScreenPlan decides; this carries it out and keeps the state that goes with it:
// the night and its place, the app in front's rule, Night light, the tone lab's settings and the visibility guard.
// The app owns it, forwards its window's messages, and adds what lies outside the pipeline (the real backlight, the
// reading strip).
class Screen {
public:
    // What it asks of the app.
    struct Hooks {
        std::function<void()> apply;              // show the screen again (the app's ApplyScreen: this, and the rest)
        std::function<void(bool screen)> changed; // a setting changed: save it soon (screen: and show it)
        std::function<void()> syncSettings;       // the settings window should mirror the settings
        std::function<void()> refreshSettings;    // ... or its status lines
    };
    // window and the message ids: where the engines and Windows location send their news.
    struct Messages { UINT located, toneFailed; };
    Screen(Settings& settings, HWND window, Messages messages, Hooks hooks);
    ~Screen();
    Screen(const Screen&) = delete;
    Screen& operator=(const Screen&) = delete;

    void Apply();          // the pipeline only (the app's ApplyScreen calls it)
    void Reset();          // after a display change or unlocking: everything set up again on the next Apply
    void Stop();           // every engine off, the screen plain
    void ClearLeftover() { tone.ClearLeftover(); }   // at start: a gamma curve a crash left behind (not in a diagnostic copy)
    void Tick();           // each second
    const Settings& Shown() const { return shown; }   // what the screen shows: the settings, or a rule's mode

    // The night schedule, its place, and Windows Night light.
    float NightShown() const;   // how far into the night (0 by day; 0 while an app's mode is in use)
    void PreviewNight();
    void PreviewNightStrength();
    float WarmthNow() const;
    std::wstring NightStatus() const;
    SettingsHost::Sync SyncState(std::wstring& text) const;
    void SetCity(const std::wstring& city);
    void Located(double lat, double lon, bool ok);
    const NightLight::State& NightLightState() const { return nightLight; }
    bool FollowsNightLight() const;
    bool NightLightWarming() const;
    Settings Scheduled() const;

    // Per-app rules.
    int RuleMode() const { return ruleMode; }
    void CheckRules();

    // Tone.
    bool EffectsAvailable() const;
    void SetToneMode(int mode);
    void RetestLut();
    double ComfortNet(double x) const { return comfort.Net(x); }
    std::wstring ComfortStatus() const;
    const std::wstring& ToneStatus() const { return tone.Status(); }
    std::wstring SmartStatus() const { return smart.Status(); }
    std::wstring LayerStatus() const { return smartLayer.Status(); }
    const std::wstring& LutStatus() const { return lut.Status(); }
    bool LutFailed() const { return lut.Failed(); }
    const std::wstring& GuardNote() const { return guardNote; }

    // The tone lab changes engine, strength and brightness freely; they come back when it closes.
    void LabBegin();
    void LabCompare(bool without);
    void LabEnd();
    bool LabForget();   // the settings back as they were, without showing them (at exit); false: no lab was on

private:
    using Clock = std::chrono::system_clock;

    Settings Effective() const;
    float NightNow() const;
    void UpdateLocation();
    void NightLightTick();
    void StartLut(const Settings& s);
    void ApplyRamp(const ScreenPlan::Ramp& ramp);
    void ApplyEngines(const Settings& s, const ScreenPlan::Engines& e, ScreenPlan::Gains gains, float bright);
    bool WriteLut();
    SmartGpu::Params SmartParams(float bright) const;

    Settings& s;
    HWND window;
    Messages messages;
    Hooks hooks;

    Settings shown;                    // what the screen shows
    float night = 1;                   // the night strength last shown
    Clock::time_point previewUntil{}, previewNightUntil{};
    int ruleMode = -1;                 // the mode a rule applies right now (-1: none)
    HWND ruleWindow = nullptr;         // that rule's app window
    Clock::time_point locatedAt{}, syncedAt{};
    NightLight::State nightLight;
    std::wstring guardNote;

    ColorFilter filter;
    GammaRamp tone;
    Comfort comfort;
    SmartFilter smart;
    SmartLayer smartLayer;
    LutGuard lut;
    Grain grain;
    std::wstring toneKey = L"off", lutKey;
    float colourGain = 1;              // overall dimming the colour filter applies right now
    bool effectShown = false;          // a colour effect is taking the blue light filter's place
    int verifyTick = 0;
    std::optional<Settings> labSaved;
    bool labCompare = false;
};

} // namespace bb
