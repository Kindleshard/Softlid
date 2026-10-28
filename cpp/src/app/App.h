#pragma once
#include "app/Screen.h"
#include "app/Tray.h"
#include "core/FocusTimer.h"
#include "core/Reminders.h"
#include "core/Settings.h"
#include "features/breaks/Eyelids.h"
#include "features/reminders/BreakCard.h"
#include "features/reminders/EyePopup.h"
#include "features/mini/MiniHost.h"
#include "features/mini/MiniMorph.h"
#include "features/mini/MiniWindow.h"
#include "features/settings/SettingsHost.h"
#include "features/settings/SettingsWindow.h"
#include "features/strip/ReadingStrip.h"
#include "features/tone/ToneLab.h"
#include "features/tone/ToneLabHost.h"
#include "features/update/Updater.h"
#include "features/update/Updates.h"
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
    void Changed(bool screenToo = false) override;
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
    int RuleMode() const override;
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
    const NightLight::State& NightLightState() const override;
    bool FollowsNightLight() const override;
    bool NightLightWarming() const override;
    Settings Scheduled() const override;

    // Tone (AppScreen.cpp).
    void SetToneMode(int mode) override;
    void RetestLut() override;
    void OpenToneLab() override;
    void LabCompare(bool without) override;
    double ComfortNet(double x) const override;
    std::wstring ComfortStatus() const override;
    const std::wstring& ToneStatus() const override;
    std::wstring SmartStatus() const override;
    std::wstring LayerStatus() const override;
    const std::wstring& LutStatus() const override;
    bool LutFailed() const override;
    const std::wstring& GuardNote() const override;

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
    bool FocusResting(float& progress, int& secondsLeft) const override;
    float BreakProgress() const override;
    float LookAwayProgress() const override;

    // Mini mode (AppExtras.cpp).
    void EnterMiniMode() override;
    void ShowTour() override;
    void LeaveMiniMode() override;
    void HideMini() override;
    void SaveMiniPosition(POINT at) override;
    void ShowMenu(bool fromMini) override;   // (also the tray icon's right-click)

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
    void SettingsClosed();
    void UpdateTrayTip();
    void Exit(const wchar_t* why);   // why goes in the run log (see RunLog)

    // Reminders (see core/Reminders for when they fall due).
    void BreakDone(bool standUp, int snoozeSeconds);
    void Pause(int minutes);   // -1: until resumed
    bool Paused() const;

    // The screen (AppScreen.cpp): the pipeline (app/Screen), and what lies outside it - the real backlight, the
    // reading strip, modes, keeping the PC awake.
    void ApplyScreen();
    void ApplyExtras(const Settings& s);
    void ReapplyScreen();      // after a display change or unlocking, when Windows may have reset the filter
    void ScreenTick();
    void ToggleFilter();
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

    std::unique_ptr<Screen> screen;    // (made once the window exists: the engines send it messages)
    std::unique_ptr<FrontWatch> frontWatch;
    Clock::time_point awakeUntil{};    // a timed keep-awake from the tray
    enum class Probe { None, Refresh, SwitchOn } probing = Probe::None;
    int lastBacklight = -1;
    std::function<void()> probeDone;   // the settings, waiting for the answer
    std::wstring backlightStatus;

    std::unique_ptr<ToneLab> lab;      // the tone lab, while open (its settings juggling is Screen's)
    bool labReopen = false;            // the settings window was open: open it again after

    // Updates.
    void WeeklyUpdateCheck();          // (when there is a release channel)
    Updates updates{[this] { Updater::Check(window, UpdateMessage); },
                    [this](const Updater::Release& r) { Updater::Install(r, window, UpdateMessage); }};
    int hourTick = 0;

    std::unique_ptr<SettingsWindow> settingsWindow;   // while open

    // Keyboard shortcuts.
    void OnHotkey(int id);
    void ResizeStrip(int direction);
    unsigned hotkeyFailed = 0;         // a bit per action whose keys another program already uses
    bool stripKeysOn = false;          // the strip's movement keys are registered (only while it stays put)

    // The focus timer.
    void FocusTick();
    FocusTimer focusTimer{settings};
    // The break showing, by priority: a higher one takes over from a lower one (which it covers); a lower one waits.
    enum class Showing { None, LookAway, StandUp, FocusRest };
    Showing showing = Showing::None;

    // The reading strip and mini mode.
    std::unique_ptr<ReadingStrip> strip;
    std::unique_ptr<MiniWindow> mini;
    std::unique_ptr<MiniMorph> morph;   // the settings window folding into it, while it does
    void ShowMini();
    HICON bigIcon = nullptr;
    bool offscreen = false;
};

} // namespace bb
