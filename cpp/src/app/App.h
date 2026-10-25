#pragma once
#include "app/Tray.h"
#include "core/Reminders.h"
#include "core/Settings.h"
#include "features/reminders/BreakCard.h"
#include "features/reminders/EyePopup.h"
#include "features/screen/ColorFilter.h"
#include "features/settings/SettingsWindow.h"
#include "platform/ForegroundApp.h"
#include <windows.h>
#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace bb {

// The tray app: a hidden window that owns the tray icon and gets the 1-second tick, the menu, and requests from a
// second copy to show the settings. Everything else (reminders, the screen filter, windows) hangs off this.
// App.cpp holds the app itself and the reminders; AppScreen.cpp the screen.
class App {
public:
    // offscreen (diagnostics): the settings open off the screen without taking focus, for a screenshot.
    App(HINSTANCE instance, HANDLE showRequest, bool openSettings, bool offscreen = false);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    int Run();                         // the message loop, until Exit
    void Changed(bool screen = false); // settings changed: saved on the next tick; screen: shown at once

    // ---- For the settings window ----
    void ShowSettings(const POINT* at = nullptr, bool optionsPage = false);
    void Rebuild(bool optionsPage);    // the settings built again (new language, accent or view), soon, in place
    void Quit();                       // Exit, chosen when closing the settings
    void SetLanguage(const std::wstring& code);
    void SetAccent(bool fromWindows, int color);
    void UseCompactView();             // the Simple view keeps only the everyday features: the others are switched off
    void ResetAll();
    bool RunAtStartup() const;
    void SetRunAtStartup(bool on);
    void SetMiniEnabled(bool on);

    void Blink(bool instant);
    void StartBreak(bool standUp);
    std::wstring BreakStatus() const;
    std::wstring StandUpStatus() const;

    void ChooseMode(int mode);
    void ManualScreenChange();         // a slider or a shortcut moved: your own values again, not a preset
    void PreviewNight();               // a blue light slider moves: show the daytime strength for a few seconds
    void PreviewNightStrength();       // ... or the night strength for a moment
    int RuleMode() const { return ruleMode; }
    bool EffectsAvailable() const { return filter.CanShowEffects(); }
    std::wstring NightStatus() const;
    enum class Sync { Syncing, Synced, Failed, CityChosen };   // how the sun times are tied to a place
    Sync SyncState(std::wstring& text) const;
    void SetCity(const std::wstring& city);   // "" goes back to syncing by itself
    bool KeepingAwake() const;
    void SetKeepAwake(bool on);
    void SetHardwareBrightness(bool on, std::function<void()> done);
    const std::wstring& BacklightStatus() const { return backlightStatus; }

private:
    using Clock = std::chrono::system_clock;

    // Messages to the hidden window (WM_APP+1 is the tray's, WM_APP+2 a break card's).
    static constexpr UINT FrontChangedMessage = WM_APP + 3;   // another window came to the front
    static constexpr UINT LocatedMessage = WM_APP + 4;        // Windows location answered (lParam: Location::Fix*)
    static constexpr UINT ProbedMessage = WM_APP + 5;         // the displays answered whether they do DDC/CI
    static constexpr UINT RebuildMessage = WM_APP + 6;        // build the settings again (wParam: on the options page)
    static constexpr UINT QuitMessage = WM_APP + 7;           // exit, chosen in the settings

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
    std::unique_ptr<BreakCard> card;   // the break showing now, if any
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

    std::unique_ptr<SettingsWindow> settingsWindow;   // while open
    HICON bigIcon = nullptr;
    bool offscreen = false;
};

} // namespace bb
