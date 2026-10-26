#pragma once
#include "app/Tray.h"
#include "core/FocusTimer.h"
#include "core/Reminders.h"
#include "core/ScreenPlan.h"
#include "core/Settings.h"
#include "features/breaks/Eyelids.h"
#include "features/reminders/BreakCard.h"
#include "features/reminders/EyePopup.h"
#include "features/screen/ColorFilter.h"
#include "features/mini/MiniHost.h"
#include "features/mini/MiniWindow.h"
#include "features/settings/SettingsHost.h"
#include "features/settings/SettingsWindow.h"
#include "features/strip/ReadingStrip.h"
#include "features/tone/Comfort.h"
#include "features/tone/GammaRamp.h"
#include "features/tone/Grain.h"
#include "features/tone/LutGuard.h"
#include "features/tone/SmartFilter.h"
#include "features/tone/SmartLayer.h"
#include "features/tone/ToneLab.h"
#include "features/tone/ToneLabHost.h"
#include "features/update/Updater.h"
#include "features/update/Updates.h"
#include "platform/NightLight.h"
#include "platform/ForegroundApp.h"
#include <windows.h>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace bb {

// The tray app: a hidden window that owns the tray icon and gets the 1-second tick, the menu, and requests from a
// second copy to show the settings. Everything else (reminders, the screen filter, windows) hangs off this.
// App.cpp holds the app itself and the reminders; AppScreen.cpp the screen.
class App : public SettingsHost, public MiniHost, public ToneLabHost {
public:
    // offscreen (diagnostics): the settings open off the screen without taking focus, for a screenshot.
    App(HINSTANCE instance, HANDLE showRequest, bool openSettings, bool offscreen = false);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    int Run();                         // the message loop, until Exit
    void ShowSettings(const POINT* at = nullptr, bool optionsPage = false);

    // What the settings, mini mode and the tone lab ask of the app (the interfaces say what each does).
    Settings& S() override { return settings; }
    const Settings& S() const { return settings; }
    void Changed(bool screen = false) override;
    void Rebuild(bool optionsPage) override;
    void Quit() override;
    void SetLanguage(const std::wstring& code) override;
    void SetAccent(bool fromWindows, int color) override;
    void UseCompactView() override;
    void ResetAll() override;
    bool RunAtStartup() const override;
    void SetRunAtStartup(bool on) override;
    void SetMiniEnabled(bool on) override;

    // Reminders (App.cpp).
    void Blink(bool instant) override;
    void StartBreak(bool standUp) override;
    std::wstring BreakStatus() const override;
    std::wstring StandUpStatus() const override;

    // The screen (AppScreen.cpp).
    void ChooseMode(int mode) override;
    void ManualScreenChange() override;
    void PreviewNight() override;
    void PreviewNightStrength() override;
    int RuleMode() const override { return ruleMode; }
    bool EffectsAvailable() const override;
    std::wstring NightStatus() const override;
    Sync SyncState(std::wstring& text) const override;
    void SetCity(const std::wstring& city) override;
    bool KeepingAwake() const override;
    void SetKeepAwake(bool on) override;
    void SetHardwareBrightness(bool on, std::function<void()> done) override;
    const std::wstring& BacklightStatus() const override { return backlightStatus; }
    float WarmthNow() const override;

    // Windows Night light (AppScreen.cpp).
    const NightLight::State& NightLightState() const override { return nightLight; }
    bool FollowsNightLight() const override;
    bool NightLightWarming() const override;
    Settings Scheduled() const override;

    // Tone (AppScreen.cpp).
    void SetToneMode(int mode) override;
    void RetestLut() override;
    void OpenToneLab() override;
    void LabCompare(bool without) override;
    double ComfortNet(double x) const override { return comfort.Net(x); }
    std::wstring ComfortStatus() const override;
    const std::wstring& ToneStatus() const override { return tone.Status(); }
    std::wstring SmartStatus() const override { return smart.Status(); }
    std::wstring LayerStatus() const override { return smartLayer.Status(); }
    const std::wstring& LutStatus() const override { return lut.Status(); }
    bool LutFailed() const override { return lut.Failed(); }
    const std::wstring& GuardNote() const override { return guardNote; }

    // Updates (AppSettings.cpp).
    const Updater::State& Update() const override { return updates.State(); }
    void CheckForUpdates() override { updates.Check(); }
    void InstallUpdate() override { updates.Install(); }

    // Keyboard shortcuts (AppShortcuts.cpp).
    void RegisterHotkeys() override;
    void SuspendHotkeys() override;
    bool HotkeyFailed(int id) const override;

    // The focus timer (AppExtras.cpp).
    Focus FocusState() const override { return focusTimer.State(); }
    int FocusLeft() const override { return focusTimer.Left(); }
    int FocusDone() const override { return focusTimer.Done(); }
    void FocusStart() override;   // (or resume)
    void FocusPause() override;
    void FocusStop() override;
    std::wstring FocusText() const override;
    float BreakProgress() const override;

    // Mini mode (AppExtras.cpp).
    void EnterMiniMode() override;
    void LeaveMiniMode() override;
    void SaveMiniPosition(POINT at) override;

private:
    using Clock = std::chrono::system_clock;

    // Messages to the hidden window (WM_APP+1 is the tray's, WM_APP+2 a break card's).
    static constexpr UINT FrontChangedMessage = WM_APP + 3;   // another window came to the front
    static constexpr UINT LocatedMessage = WM_APP + 4;        // Windows location answered (lParam: Location::Fix*)
    static constexpr UINT ProbedMessage = WM_APP + 5;         // the displays answered whether they do DDC/CI
    static constexpr UINT RebuildMessage = WM_APP + 6;        // build the settings again (wParam: on the options page)
    static constexpr UINT QuitMessage = WM_APP + 7;           // exit, chosen in the settings
    static constexpr UINT LeaveMiniMessage = WM_APP + 8;      // mini mode closes and the settings open
    static constexpr UINT ToneFailedMessage = WM_APP + 9;     // a Smart engine gave up: the standard curve takes over
    static constexpr UINT LabOpenMessage = WM_APP + 10;       // the tone lab opens in the settings' place
    static constexpr UINT UpdateMessage = WM_APP + 12;        // from the updater (lParam: Updater::Result*)

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w, LPARAM l);
    LRESULT Handle(UINT message, WPARAM w, LPARAM l);

    void OnTick();
    void ShowMenu();
    void SettingsClosed();
    void UpdateTrayTip();
    void Exit();

    // Reminders (see core/Reminders for when they fall due).
    void BreakDone(bool standUp, int snoozeSeconds);
    void Pause(int minutes);   // -1: until resumed
    bool Paused() const;

    // The screen (AppScreen.cpp): the filter, the night schedule and its place, modes and per-app rules, keeping
    // the PC awake, and the real backlight.
    void ApplyScreen();
    void StartLut(const Settings& s);
    void ApplyRamp(const ScreenPlan::Ramp& ramp);
    void ApplyEngines(const Settings& s, const ScreenPlan::Engines& e, ScreenPlan::Gains gains, float bright);
    void ApplyExtras(const Settings& s);
    void ReapplyScreen();      // after a display change or unlocking, when Windows may have reset the filter
    void ScreenTick();
    float NightNow() const;    // how far into the night schedule it is: 0 by day (or with no schedule), 1 at night
    float NightShown() const;  // the same, but 0 while an app's mode is in use (that mode shows as it is)
    void ToggleFilter();
    Settings Effective() const;
    void CheckRules();
    void UpdateLocation();
    void Located(double lat, double lon, bool ok);
    void KeepAwakeFor(int minutes);
    void Probed(int supported, int total, int current);

    HINSTANCE instance;
    HANDLE showRequest;   // set by a second copy of BlinkBuddy
    HWND window = nullptr;
    HICON icon = nullptr;
    UINT taskbarCreated = 0;
    Tray tray;
    Settings settings;
    bool dirty = false;

    Reminders reminders{settings};
    EyePopup eye;
    std::unique_ptr<Window> card;      // the break showing now, if any (a card, the guided exercises or the stretch coach)
    std::unique_ptr<Eyelids> lids;     // the eyelids blink style (made when first used)
    Clock::time_point pausedUntil{};

    ColorFilter filter;
    Settings screenNow;                // what the screen shows: the settings, or a copy with a rule's mode applied
    float night = 1;                   // the night strength last shown
    Clock::time_point previewUntil{}, previewNightUntil{};
    int ruleMode = -1;                 // the mode a rule applies right now (-1: none)
    HWND ruleWindow = nullptr;         // that rule's app window
    std::unique_ptr<FrontWatch> frontWatch;
    Clock::time_point locatedAt{}, syncedAt{};
    Clock::time_point awakeUntil{};    // a timed keep-awake from the tray
    enum class Probe { None, Refresh, SwitchOn } probing = Probe::None;
    int lastBacklight = -1;
    std::function<void()> probeDone;   // the settings, waiting for the answer
    std::wstring backlightStatus, guardNote;

    // Tone: the engines, and the test lab (which changes engine, strength and brightness freely; they come back
    // when it closes).
    void StopTone();           // every engine off, the screen plain
    bool WriteLut();
    SmartGpu::Params SmartParams(float bright) const;
    void LabBegin();
    void LabEnd();
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
    bool labCompare = false, labReopen = false;
    std::unique_ptr<ToneLab> lab;

    // Updates.
    void WeeklyUpdateCheck();          // (when there is a release channel)
    Updates updates{[this] { Updater::Check(window, UpdateMessage); },
                    [this](const Updater::Release& r) { Updater::Install(r, window, UpdateMessage); }};
    int hourTick = 0;

    // Windows Night light.
    void NightLightTick();             // each second: Night light read again (Windows sends no change notice)
    NightLight::State nightLight;

    std::unique_ptr<SettingsWindow> settingsWindow;   // while open

    // Keyboard shortcuts.
    void OnHotkey(int id);
    void ResizeStrip(int direction);
    unsigned hotkeyFailed = 0;         // a bit per action whose keys another program already uses
    bool stripKeysOn = false;          // the strip's movement keys are registered (only while it stays put)

    // The focus timer.
    void FocusTick();
    FocusTimer focusTimer{settings};
    bool restCard = false;             // the break card showing is the focus timer's rest

    // The reading strip and mini mode.
    std::unique_ptr<ReadingStrip> strip;
    std::unique_ptr<MiniWindow> mini;
    void ShowMini();
    HICON bigIcon = nullptr;
    bool offscreen = false;
};

} // namespace bb
