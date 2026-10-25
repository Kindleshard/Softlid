// The screen side of the app: the colour filter and what feeds it - the night schedule and its place, screen modes
// and per-app rules, the visibility guard - plus keeping the PC awake and the real backlight.
#include "app/App.h"
#include "core/Lang.h"
#include "core/Modes.h"
#include "core/Night.h"
#include "core/VisibilityGuard.h"
#include "features/screen/Backlight.h"
#include "platform/KeepAwake.h"
#include "platform/Location.h"
#include <algorithm>
#include <cmath>

namespace bb {

void App::ApplyScreen()
{
    // The screen shows your settings, or (while an app with a rule is in front) a copy with its mode applied.
    screenNow = Effective();
    const Settings& s = screenNow;
    bool on = s.FilterEnabled;

    // (The tone engines come in chunk 8: until then a grey on screen is simply brightness times grey.)
    VisibilityGuard::Result guard;
    if (on) guard = VisibilityGuard::Solve(s.Brightness / 100.0, [](double b, double, double x) { return b * x; });
    if (guard.ease < 1)
        guardNote = Lang::T(L"Visibility guard: brightness at 100% and the curve eased so dark details stay visible.");
    else if (guard.brightness > s.Brightness / 100.0 + 0.005)
        guardNote = Lang::F(L"Visibility guard: brightness held at {0:0}% (set to {1}%) so the screen stays readable.",
                            {std::to_wstring(std::lround(guard.brightness * 100)), std::to_wstring(s.Brightness)});
    else
        guardNote.clear();

    night = NightShown();
    filter.Apply(s, night, 1, static_cast<float>(guard.brightness));

    int backlight = s.HardwareBrightness ? s.Backlight : -1;
    if (backlight >= 0 && backlight != lastBacklight) {
        lastBacklight = backlight;
        Backlight::Set(backlight);
    }

    // The reading strip (made when first switched on), and its movement keys while it stays put.
    if (s.StripEnabled && !strip) strip = std::make_unique<ReadingStrip>();
    if (strip) strip->Apply(s, window);
    if (stripKeysOn != (s.StripEnabled && !s.StripFollow)) RegisterHotkeys();
}

void App::ReapplyScreen()
{
    // Built again from scratch: the displays (and their sizes) may have changed.
    filter.Off();
    ApplyScreen();
}

// Each second: the night schedule moves on (or a preview of it ends), the app in front may have a rule, and a timed
// keep-awake may be over.
void App::ScreenTick()
{
    UpdateLocation();
    if (std::abs(NightShown() - night) > 0.004f) ApplyScreen();
    CheckRules();
    if (awakeUntil != Clock::time_point{} && Clock::now() >= awakeUntil) {
        awakeUntil = {};
        KeepAwake::Set(settings.KeepAwake);
    }
}

// ---- The night schedule ----

float App::NightNow() const
{
    auto now = Clock::now();
    if (now < previewNightUntil) return 1;   // a moment's preview of the night strength
    if (!settings.ScheduleEnabled) return 0;
    if (now < previewUntil) return 0;
    SYSTEMTIME local;
    GetLocalTime(&local);
    return Night::Factor(settings, local);
}

float App::NightShown() const { return ruleMode >= 0 ? 0 : NightNow(); }

// While a blue light slider is being moved, show what it sets, whatever the time of day: the daytime one for a few
// seconds; the schedule's night one only for a moment, then back to the daytime one until night comes.
void App::PreviewNight()
{
    previewUntil = Clock::now() + std::chrono::seconds(5);
    previewNightUntil = {};
}

void App::PreviewNightStrength() { previewNightUntil = Clock::now() + std::chrono::milliseconds(1200); }

// Sunset to sunrise needs to know where the PC is: ask Windows location now and then (a few times a day is plenty),
// keeping the last answer when location is off.
void App::UpdateLocation()
{
    auto now = Clock::now();
    if (!settings.ScheduleEnabled || settings.ScheduleMode != 0 || settings.CityManual) return;
    if (locatedAt != Clock::time_point{} && now - locatedAt < std::chrono::hours(6)) return;
    locatedAt = now;
    Location::Locate(window, LocatedMessage);
}

// The blue light filter in effect right now (0 when the filter is off, or while a colour effect takes its place).
float App::WarmthNow() const
{
    const Settings& v = screenNow;
    bool effectShown = v.FilterEnabled && v.ColorEffect != 0 && filter.CanShowEffects();
    return v.FilterEnabled && !effectShown ? static_cast<float>(Night::Blue(v, NightShown()) / 100) : 0.0f;
}

std::wstring App::NightStatus() const
{
    SYSTEMTIME local;
    GetLocalTime(&local);
    return Night::Status(settings, local);
}

// The line under the schedule's status: still syncing, synced (with Windows location or the time zone's city), failed
// (neither works: a city has to be chosen), or a city you chose.
App::Sync App::SyncState(std::wstring& text) const
{
    bool windows;
    std::wstring place = Night::Place(settings, windows);
    if (settings.CityManual && !place.empty()) {
        text = Lang::F(L"{0} time, chosen by you", {place});
        return Sync::CityChosen;
    }
    if (syncedAt == Clock::time_point{} && !settings.CityManual) {
        text = Lang::T(L"Syncing with your location...");
        return Sync::Syncing;
    }
    if (windows) {
        text = Lang::T(L"Synced with your Windows location");
        return Sync::Synced;
    }
    if (!place.empty()) {
        text = Lang::F(L"Synced with {0} time", {place});
        return Sync::Synced;
    }
    text = Lang::T(L"Couldn't sync.");
    return Sync::Failed;
}

void App::SetCity(const std::wstring& city)
{
    settings.CityManual = !city.empty();
    settings.City = city;
    syncedAt = locatedAt = {};   // automatic again: looked up on the next tick
    Changed(true);
}

void App::Located(double lat, double lon, bool ok)
{
    syncedAt = Clock::now();   // whichever answered: Windows location, or (without it) the time zone
    if (!ok || settings.CityManual) return;
    std::wstring where = Night::Coordinates(lat, lon);
    if (where == settings.City) return;
    settings.City = where;
    Changed(true);
}

// ---- Modes and per-app rules ----

void App::ToggleFilter()
{
    settings.FilterEnabled = !settings.FilterEnabled;
    ManualScreenChange();
    Changed(true);
    if (settingsWindow) settingsWindow->SyncFromApp();
}

void App::ChooseMode(int mode)
{
    Modes::Choose(settings, mode);
    Changed(true);
    if (settingsWindow) settingsWindow->SyncFromApp();
}

void App::ManualScreenChange()
{
    if (settings.Mode != Modes::Custom) settings.Mode = Modes::Custom;
}

Settings App::Effective() const
{
    Settings v = settings;
    if (ruleMode >= 0) Modes::Apply(v, ruleMode);
    return v;
}

// Is an app with a rule in front? Checked the moment another window comes to the front (and each second, to be
// sure). A window that only passes through - the taskbar, Start, Alt+Tab, BlinkBuddy itself - leaves the app's mode
// on while the app's window is still there to see.
void App::CheckRules()
{
    if (settings.AppRules.empty() && ruleMode < 0) return;
    int mode;
    std::wstring front;
    HWND frontWindow;
    if (!settings.AdvancedView) mode = -1;   // (an Advanced feature: kept, but idle in Simple)
    else if (ForegroundApp::Get(front, frontWindow)) {
        mode = Modes::RuleFor(settings, front);
        ruleWindow = mode >= 0 ? frontWindow : nullptr;
    } else
        mode = ruleMode >= 0 && ForegroundApp::StillShown(ruleWindow) ? ruleMode : -1;
    if (mode == ruleMode) return;
    ruleMode = mode;
    ApplyScreen();
}

// ---- Keeping the PC awake ----

bool App::KeepingAwake() const { return settings.KeepAwake || Clock::now() < awakeUntil; }

void App::SetKeepAwake(bool on)
{
    settings.KeepAwake = on;
    awakeUntil = {};
    Changed();
    KeepAwake::Set(on);
    if (settingsWindow) settingsWindow->SyncFromApp();
}

void App::KeepAwakeFor(int minutes)
{
    settings.KeepAwake = false;
    awakeUntil = Clock::now() + std::chrono::minutes(minutes);
    Changed();
    KeepAwake::Set(true);
    if (settingsWindow) settingsWindow->SyncFromApp();
}

// ---- The real backlight ----

// Switching on first asks the displays (on a worker thread) whether they answer over DDC/CI.
void App::SetHardwareBrightness(bool on, std::function<void()> done)
{
    if (!on) {
        if (settings.HardwareBrightness && settings.HardwareRestore >= 0) Backlight::Set(settings.HardwareRestore);
        settings.HardwareBrightness = false;
        lastBacklight = -1;
        backlightStatus.clear();
        probing = Probe::None;   // (an answer still on its way is ignored)
        probeDone = nullptr;
        Changed(true);
        if (done) done();
        return;
    }
    probeDone = std::move(done);
    backlightStatus = Lang::T(L"Checking your monitors...");
    probing = Probe::SwitchOn;
    Backlight::Probe(window, ProbedMessage);
}

void App::Probed(int supported, int total, int current)
{
    Probe was = probing;
    probing = Probe::None;
    if (was == Probe::None) return;
    auto done = std::move(probeDone);
    probeDone = nullptr;
    if (supported == 0) {
        backlightStatus = was == Probe::SwitchOn
            ? Lang::T(L"No display answered. On external monitors, DDC/CI may be off in the monitor's menu.")
            : Lang::T(L"No display answered just now.");
        if (done) done();
        return;
    }
    backlightStatus = supported == total
        ? (total == 1 ? Lang::T(L"Controlling the backlight of 1 display.")
                      : Lang::F(L"Controlling the backlight of {0} displays.", {std::to_wstring(total)}))
        : Lang::F(L"Controlling the backlight of {0} of {1} displays.", {std::to_wstring(supported), std::to_wstring(total)});
    if (was == Probe::SwitchOn) {
        settings.HardwareBrightness = true;
        settings.HardwareRestore = settings.Backlight = lastBacklight = current;
        Changed(true);
    }
    if (done) done();
}

} // namespace bb
