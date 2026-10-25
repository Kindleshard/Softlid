#pragma once
#include "app/Tray.h"
#include "core/Reminders.h"
#include "core/Settings.h"
#include "features/breaks/Eyelids.h"
#include "features/reminders/BreakCard.h"
#include "features/reminders/EyePopup.h"
#include "features/screen/ColorFilter.h"
#include "features/mini/MiniWindow.h"
#include "features/settings/SettingsWindow.h"
#include "features/strip/ReadingStrip.h"
#include "features/tone/Comfort.h"
#include "features/tone/GammaRamp.h"
#include "features/tone/Grain.h"
#include "features/tone/LutGuard.h"
#include "features/tone/SmartFilter.h"
#include "features/tone/SmartLayer.h"
#include "features/tone/ToneLab.h"
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
class App {
public:
    // offscreen (diagnostics): the settings open off the screen without taking focus, for a screenshot.
    App(HINSTANCE instance, HANDLE showRequest, bool openSettings, bool offscreen = false);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    int Run();                         // the message loop, until Exit
    void Changed(bool screen = false); // settings changed: saved on the next tick; screen: shown at once
    Settings& S() { return settings; }
    const Settings& S() const { return settings; }

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
    bool EffectsAvailable() const;     // colour effects can show: not while the Smart (GPU) engine does the work, nor with the overlays
    std::wstring NightStatus() const;
    enum class Sync { Syncing, Synced, Failed, CityChosen };   // how the sun times are tied to a place
    Sync SyncState(std::wstring& text) const;
    void SetCity(const std::wstring& city);   // "" goes back to syncing by itself
    bool KeepingAwake() const;
    void SetKeepAwake(bool on);
    void SetHardwareBrightness(bool on, std::function<void()> done);
    const std::wstring& BacklightStatus() const { return backlightStatus; }
    float WarmthNow() const;           // the blue light filter in effect now (0..1), for things drawn to match it

    // ---- Tone (AppScreen.cpp) ----
    void SetToneMode(int mode);        // an engine chosen (clears the engines' failures)
    void RetestLut();                  // DesktopLUT's 15-second check again (e.g. after choosing another copy)
    void OpenToneLab();                // (soon: it takes the settings window's place)
    void LabCompare(bool without);     // the lab's hold-to-compare: the screen without tone while held
    double ComfortNet(double x) const { return comfort.Net(x); }
    std::wstring ComfortStatus() const;
    const std::wstring& ToneStatus() const { return tone.Status(); }
    std::wstring SmartStatus() const { return smart.Status(); }
    std::wstring LayerStatus() const { return smartLayer.Status(); }
    const std::wstring& LutStatus() const { return lut.Status(); }
    bool LutFailed() const { return lut.Failed(); }
    const std::wstring& GuardNote() const { return guardNote; }   // while the visibility guard holds brightness up

    // ---- Keyboard shortcuts (AppShortcuts.cpp) ----
    void RegisterHotkeys();            // (again) from the settings
    void SuspendHotkeys();             // all off for a moment (while the shortcuts window records new keys)
    bool HotkeyFailed(int id) const;   // another program already uses those keys

    // ---- The focus timer: work, then rest, again and again (AppExtras.cpp) ----
    enum class Focus { Off, Working, Paused, Resting };
    Focus FocusState() const { return focus; }
    int FocusLeft() const { return focusLeft; }
    int FocusDone() const { return focusDone; }   // work periods finished since BlinkBuddy started
    void FocusStart();                 // (or resume)
    void FocusPause();
    void FocusStop();
    std::wstring FocusText() const;
    float BreakProgress() const;       // how far the wait for the next break is (0..1), for mini mode's rim; -1: none

    // ---- Mini mode (AppExtras.cpp) ----
    void EnterMiniMode();
    void LeaveMiniMode();              // and the settings open
    void SaveMiniPosition(POINT at);

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
    static constexpr UINT LabClosedMessage = WM_APP + 11;     // ... and closed: settings back as they were

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

    std::unique_ptr<SettingsWindow> settingsWindow;   // while open

    // Keyboard shortcuts.
    void OnHotkey(int id);
    void ResizeStrip(int direction);
    unsigned hotkeyFailed = 0;         // a bit per action whose keys another program already uses
    bool stripKeysOn = false;          // the strip's movement keys are registered (only while it stays put)

    // The focus timer.
    void FocusTick();
    Focus focus = Focus::Off;
    int focusLeft = 0, focusDone = 0;
    bool restCard = false;             // the break card showing is the focus timer's rest

    // The reading strip and mini mode.
    std::unique_ptr<ReadingStrip> strip;
    std::unique_ptr<MiniWindow> mini;
    void ShowMini();
    HICON bigIcon = nullptr;
    bool offscreen = false;
};

} // namespace bb
