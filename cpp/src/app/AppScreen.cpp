// The screen side of the app: the colour filter and what feeds it - the night schedule and its place, screen modes
// and per-app rules, the visibility guard - plus keeping the PC awake and the real backlight.
#include "app/App.h"
#include "core/Lang.h"
#include "core/Modes.h"
#include "core/Night.h"
#include "core/ColorMath.h"
#include "core/ToneCurve.h"
#include "core/ScreenPlan.h"
#include "core/VisibilityGuard.h"
#include "features/screen/Backlight.h"
#include "features/tone/LutFiles.h"
#include "platform/Clock.h"
#include "platform/KeepAwake.h"
#include "platform/Location.h"
#include <algorithm>
#include <cmath>

namespace bb {

// The screen, from the settings (or a rule's mode) to every engine: the decisions are ScreenPlan's, carried out here.
void App::ApplyScreen()
{
    screenNow = Effective();   // your settings, or (while an app with a rule is in front) a copy with its mode applied
    const Settings& s = screenNow;
    if (ScreenPlan::WantsLut(s, labCompare)) StartLut(s);
    ScreenPlan::Engines e = ScreenPlan::Choose(s, {labCompare, smart.Failed(), smartLayer.Failed(), lut.Failed(), filter.CanShowEffects()});
    if (!e.lut) lut.Stop();
    effectShown = e.effectShown;
    comfort.Apply(s, e.preset);

    double white = comfort.White();
    VisibilityGuard::Result guard = ScreenPlan::Guard(s, e, white, labCompare);
    guardNote = ScreenPlan::GuardNote(guard, s.Brightness);
    ScreenPlan::Ramp ramp = ScreenPlan::RampFor(s, e, guard);
    ApplyRamp(ramp);
    ScreenPlan::Gains gains = ScreenPlan::FilterGains(e, ramp, tone.Prescale(), tone.Refused(), white);

    night = NightShown();
    ApplyEngines(s, e, gains, ramp.bright);
    ApplyExtras(s);
}

// DesktopLUT: a strength change writes that strength's table and restarts DesktopLUT with it. Starting it may fail at
// once (not found, permission declined); ScreenPlan then falls back.
void App::StartLut(const Settings& s)
{
    bool written = WriteLut();
    if (lut.Running()) {
        if (written) lut.Reload();
    } else lut.Ensure(s, false);
}

// The gamma ramp, handed to Windows only when it changes.
void App::ApplyRamp(const ScreenPlan::Ramp& ramp)
{
    if (ramp.key == toneKey) return;
    toneKey = ramp.key;
    if (ramp.kind == ScreenPlan::Ramp::Kind::None) tone.Off();
    else tone.SetCurve(ramp);
}

// The colour filter or the Smart (GPU) filter, the Smart layer, and grain.
void App::ApplyEngines(const Settings& s, const ScreenPlan::Engines& e, ScreenPlan::Gains gains, float bright)
{
    if (e.smart) {
        filter.Off();
        smart.Apply(SmartParams(bright), window, ToneFailedMessage);
        colourGain = 1;
    } else {
        smart.Stop();
        filter.Apply(s, night, gains.prescale, gains.onScreen);
        colourGain = e.on ? gains.prescale * gains.onScreen : 1.0f;
    }
    float dpi = GetDpiForSystem() / 96.0f;
    bool bakeGrain = ScreenPlan::BakesGrain(s, e);
    if (e.layer) {
        SmartLayer::Params p;
        p.level = s.Level();
        p.gain = colourGain;
        p.grain = bakeGrain;
        p.grainAlpha = static_cast<int>(std::lround(s.GrainStrength * 1.28));
        p.grainTone = s.GrainTone;
        p.grainBlock = s.GrainCoarse ? std::max(2, static_cast<int>(std::lround(2 * dpi))) : 1;
        smartLayer.Apply(p, window, ToneFailedMessage);
    } else smartLayer.Stop();
    grain.Apply(s, dpi, bakeGrain);
}

// The real backlight, and the reading strip (made when first switched on) with its movement keys while it stays put.
void App::ApplyExtras(const Settings& s)
{
    int backlight = s.HardwareBrightness ? s.Backlight : -1;
    if (backlight >= 0 && backlight != lastBacklight) {
        lastBacklight = backlight;
        Backlight::Set(backlight);
    }
    if (s.StripEnabled && !strip) strip = std::make_unique<ReadingStrip>();
    if (strip) strip->Apply(s, window);
    if (stripKeysOn != (s.StripEnabled && !s.StripFollow)) RegisterHotkeys();
}

void App::ReapplyScreen()
{
    // Built again from scratch: the displays (and their sizes) may have changed.
    smart.Stop();
    smartLayer.Stop();
    filter.Off();
    tone.Reset();
    toneKey.clear();
    grain.Invalidate();
    ApplyScreen();
}

void App::StopTone()
{
    smart.Stop();
    smartLayer.Stop();
    lut.Stop();
    grain.Close();
    tone.Off();
    filter.Off();
}

// ---- Tone ----

bool App::EffectsAvailable() const
{
    return filter.CanShowEffects() && !(settings.ToneEnabled && settings.ToneMode == 2 && !smart.Failed());
}

void App::SetToneMode(int mode)
{
    settings.ToneMode = mode;
    if (mode != 0 && settings.ComfortLevel == Settings::CustomLevel) settings.ComfortLevel = 1;   // custom is standard-only
    smart.ClearFailure();
    smartLayer.ClearFailure();
    lut.ClearFailure();
    Changed(true);
}

// Writes the table for the current strength (or the do-nothing one the lab compares against) if it isn't the one
// DesktopLUT was last given.
bool App::WriteLut()
{
    int level = labCompare ? -1 : settings.Level();
    std::wstring key = level < 0 ? L"plain" : ToneCurve::Levels[level];
    std::wstring path = LutFiles::CubePath();
    if (key == lutKey && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return false;
    if (!LutFiles::WriteLevel(path, level)) return false;
    lutKey = key;
    return true;
}

void App::RetestLut()
{
    lut.Stop();
    lut.ClearFailure();
    dirty = true;
    if (settings.ToneMode == 4) {
        settings.FilterEnabled = settings.ToneEnabled = true;   // the check is meaningless with tone off
        WriteLut();
        lut.Ensure(settings, true);
    }
    ApplyScreen();
}

std::wstring App::ComfortStatus() const
{
    if (tone.Refused()) return Lang::T(L"Windows won't accept a gamma curve here (HDR may be on), so only the even dimming is active.");
    std::wstring white = std::to_wstring(std::lround(comfort.White() * 100));
    return Lang::F(comfort.Adapting()
                       ? L"Whites at {0:0}% right now, a little lower because the screen is mostly bright.\nDark tones go deeper; bright shades spread apart, so text and colours stay clear."
                       : L"Whites at {0:0}% right now.\nDark tones go deeper; bright shades spread apart, so text and colours stay clear.",
                   {white});
}

SmartGpu::Params App::SmartParams(float bright) const
{
    ToneCurve::SmartParams level = ToneCurve::ForLevel(settings.Level());
    SmartGpu::Params p;
    p.compress = level.compress;
    p.expand = level.expand;
    p.dimStops = level.dimStops;
    ColorFilter::Scale(screenNow, night, bright, p.r, p.g, p.b);   // (a rule's mode, if one applies)
    p.invert = settings.Invert;
    return p;
}

// The lab takes the settings window's place, which reopens afresh when the lab closes.
void App::OpenToneLab() { PostMessageW(window, LabOpenMessage, 0, 0); }   // (not from inside the settings' own click)

void App::LabBegin()
{
    labSaved = settings;
    settings.FilterEnabled = settings.ToneEnabled = true;
    if (settings.CustomTone()) settings.ComfortLevel = 1;
    Changed(true);
}

// Hold to see the screen without tone (DesktopLUT keeps running, with a table that changes nothing).
void App::LabCompare(bool without)
{
    labCompare = without;
    ApplyScreen();
}

void App::LabEnd()
{
    labCompare = false;
    if (!labSaved) return;
    std::wstring confirmed = settings.LutConfirmedBuild;   // a DesktopLUT check passed in the lab still counts
    settings = *labSaved;
    settings.LutConfirmedBuild = confirmed;
    labSaved.reset();
    SetToneMode(settings.ToneMode);   // clears engine failures from the lab and reapplies
}

// Each second: the night schedule moves on (or a preview of it ends), the app in front may have a rule, and a timed
// keep-awake may be over.
void App::ScreenTick()
{
    UpdateLocation();
    NightLightTick();
    if (std::abs(NightShown() - night) > 0.004f) ApplyScreen();
    if (tone.Active() && ++verifyTick % 5 == 0) tone.Verify();
    lut.Poll();
    smart.KeepOnTop();
    smartLayer.KeepOnTop();
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
    Settings v = Scheduled();
    if (!v.ScheduleEnabled) return 0;
    if (now < previewUntil) return 0;
    return Night::Factor(v, LocalNow());
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

// The blue light filter in effect right now (0 when the filter is off, or while a colour effect takes its place;
// Windows Night light's while that is warming the screen).
float App::WarmthNow() const
{
    if (NightLightWarming()) return NightLight::BlueFor(nightLight.kelvin) / 100.0f;   // Night light's warmth instead
    const Settings& v = screenNow;
    return v.FilterEnabled && !effectShown ? static_cast<float>(Night::Blue(v, NightShown()) / 100) : 0.0f;
}

std::wstring App::NightStatus() const
{
    return Night::Status(Scheduled(), LocalNow());
}

// The line under the schedule's status: still syncing, synced (with Windows location or the time zone's city), failed
// (neither works: a city has to be chosen), or a city you chose.
App::Sync App::SyncState(std::wstring& text) const
{
    bool windows;
    std::wstring place = Night::Place(settings, TimeZoneId(), windows);
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
    Settings v = Scheduled();
    if (ruleMode >= 0) Modes::Apply(v, ruleMode);
    if (NightLightWarming()) v.BlueFilter = v.NightBlue = 0;   // Night light does the warming: no second layer of it
    return v;
}

// ---- Windows Night light ----

// The settings with the night schedule as it runs: Windows Night light's hours and strength while it has a schedule
// and BlinkBuddy follows it (the schedule's own settings stay as set, for when it doesn't).
Settings App::Scheduled() const
{
    Settings v = settings;
    if (!FollowsNightLight()) return v;
    v.ScheduleEnabled = true;
    v.ScheduleMode = 1;
    v.NightFrom = nightLight.Start();
    v.NightTo = nightLight.End();
    v.NightBlue = NightLight::BlueFor(nightLight.kelvin);
    v.NightFade = 0;   // (Night light switches at those times)
    return v;
}

bool App::FollowsNightLight() const { return settings.NightLightSync && nightLight.found && nightLight.scheduled; }
bool App::NightLightWarming() const { return settings.NightLightSync && nightLight.found && nightLight.on; }

// Each second: Night light read again, in case it changed (in Windows Settings, or its schedule switched it).
// Windows sends no notice of these changes; two small registry reads a second cost next to nothing.
void App::NightLightTick()
{
    NightLight::State now = NightLight::Read();
    if (now == nightLight) return;
    nightLight = now;
    ApplyScreen();
    if (settingsWindow) settingsWindow->SyncFromApp();
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
