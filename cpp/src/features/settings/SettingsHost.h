#pragma once
#include "core/Focus.h"
#include "core/Settings.h"
#include "features/update/Updater.h"
#include "platform/NightLight.h"
#include <windows.h>
#include <functional>
#include <string>

namespace bb {

// What the settings window and its dialogs need from the app (App implements it). The settings are the app's
// controls, so this is most of what the app can do - but only that: the window never sees the app's engines and
// windows.
class SettingsHost {
public:
    virtual Settings& S() = 0;
    virtual void Changed(bool screen = false) = 0;    // settings changed: saved soon; screen: shown at once
    virtual void Rebuild(bool optionsPage) = 0;        // the settings built again (new language, accent or view)
    virtual void Quit() = 0;                           // Exit, chosen when closing the settings
    virtual void SetLanguage(const std::wstring& code) = 0;
    virtual void SetAccent(bool fromWindows, int color) = 0;
    virtual void UseCompactView() = 0;                 // Simple keeps only the everyday features: the others go off
    virtual void ResetAll() = 0;
    virtual bool RunAtStartup() const = 0;
    virtual void SetRunAtStartup(bool on) = 0;
    virtual void SetMiniEnabled(bool on) = 0;
    virtual void EnterMiniMode() = 0;

    // Reminders.
    virtual void Blink(bool instant) = 0;
    virtual void StartBreak(bool standUp) = 0;
    virtual std::wstring BreakStatus() const = 0;
    virtual std::wstring StandUpStatus() const = 0;

    // The screen.
    virtual void ChooseMode(int mode) = 0;
    virtual void ManualScreenChange() = 0;             // a slider or a shortcut moved: your own values, not a preset
    virtual void PreviewNight() = 0;                   // a blue light slider moves: the daytime strength for a while
    virtual void PreviewNightStrength() = 0;           // ... or the night strength for a moment
    virtual int RuleMode() const = 0;                  // the mode a rule applies now (-1: none)
    virtual bool EffectsAvailable() const = 0;
    virtual float WarmthNow() const = 0;
    virtual bool KeepingAwake() const = 0;
    virtual void SetKeepAwake(bool on) = 0;
    virtual void SetHardwareBrightness(bool on, std::function<void()> done) = 0;
    virtual const std::wstring& BacklightStatus() const = 0;

    // The night schedule and its place, and Windows Night light.
    enum class Sync { Syncing, Synced, Failed, CityChosen };   // how the sun times are tied to a place
    virtual std::wstring NightStatus() const = 0;
    virtual Sync SyncState(std::wstring& text) const = 0;
    virtual void SetCity(const std::wstring& city) = 0;        // "" goes back to syncing by itself
    virtual const NightLight::State& NightLightState() const = 0;
    virtual bool FollowsNightLight() const = 0;
    virtual bool NightLightWarming() const = 0;
    virtual Settings Scheduled() const = 0;                     // the settings with the night schedule as it runs

    // Tone.
    virtual void SetToneMode(int mode) = 0;
    virtual void RetestLut() = 0;
    virtual void OpenToneLab() = 0;
    virtual double ComfortNet(double x) const = 0;
    virtual std::wstring ComfortStatus() const = 0;
    virtual const std::wstring& ToneStatus() const = 0;
    virtual std::wstring SmartStatus() const = 0;
    virtual std::wstring LayerStatus() const = 0;
    virtual const std::wstring& LutStatus() const = 0;
    virtual bool LutFailed() const = 0;
    virtual const std::wstring& GuardNote() const = 0;

    // Updates.
    virtual const Updater::State& Update() const = 0;
    virtual void CheckForUpdates() = 0;
    virtual void InstallUpdate() = 0;

    // Keyboard shortcuts.
    virtual void RegisterHotkeys() = 0;
    virtual void SuspendHotkeys() = 0;                 // all off while the shortcuts window records new keys
    virtual bool HotkeyFailed(int id) const = 0;       // another program already uses those keys

    // The focus timer.
    virtual Focus FocusState() const = 0;
    virtual int FocusLeft() const = 0;
    virtual int FocusDone() const = 0;
    virtual void FocusStart() = 0;
    virtual void FocusPause() = 0;
    virtual void FocusStop() = 0;
    virtual std::wstring FocusText() const = 0;

protected:
    ~SettingsHost() = default;
};

} // namespace bb
