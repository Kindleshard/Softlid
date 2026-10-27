#include "app/Screen.h"
#include "core/ColorMath.h"
#include "core/Lang.h"
#include "core/Modes.h"
#include "core/Night.h"
#include "core/ToneCurve.h"
#include "core/VisibilityGuard.h"
#include "features/tone/LutFiles.h"
#include "platform/Clock.h"
#include "platform/ForegroundApp.h"
#include "platform/Location.h"
#include <algorithm>
#include <cmath>

namespace bb {

Screen::Screen(Settings& settings, HWND window, Messages messages, Hooks hooks)
    : s(settings), window(window), messages(messages), hooks(std::move(hooks))
{
    comfort.changed = [this] { this->hooks.apply(); };
    comfort.currentGain = [this] { return colourGain; };
    lut.changed = [this] {
        this->hooks.apply();
        this->hooks.refreshSettings();
    };
    lut.confirmed = [this](const std::wstring& build) {
        s.LutConfirmedBuild = build;   // no new check needed until Windows changes
        this->hooks.changed(false);
    };
    nightLight = NightLight::Read();
}

Screen::~Screen() { Stop(); }

// The screen, from the settings (or a rule's mode) to every engine: the decisions are ScreenPlan's, carried out here.
void Screen::Apply()
{
    shown = Effective();   // your settings, or (while an app with a rule is in front) a copy with its mode applied
    const Settings& v = shown;
    if (ScreenPlan::WantsLut(v, labCompare)) StartLut(v);
    ScreenPlan::Engines e = ScreenPlan::Choose(v, {labCompare, smart.Failed(), smartLayer.Failed(), lut.Failed(), filter.CanShowEffects()});
    if (!e.lut) lut.Stop();
    effectShown = e.effectShown;
    comfort.Apply(v, e.preset);

    double white = comfort.White();
    VisibilityGuard::Result guard = ScreenPlan::Guard(v, e, white, labCompare);
    guardNote = ScreenPlan::GuardNote(guard, v.Brightness);
    ScreenPlan::Ramp ramp = ScreenPlan::RampFor(v, e, guard);
    ApplyRamp(ramp);
    ScreenPlan::Gains gains = ScreenPlan::FilterGains(e, ramp, tone.Prescale(), tone.Refused(), white);

    night = NightShown();
    ApplyEngines(v, e, gains, ramp.bright);
}

// DesktopLUT: a strength change writes that strength's table and restarts DesktopLUT with it. Starting it may fail at
// once (not found, permission declined); ScreenPlan then falls back.
void Screen::StartLut(const Settings& v)
{
    bool written = WriteLut();
    if (lut.Running()) {
        if (written) lut.Reload();
    } else lut.Ensure(v, false);
}

// The gamma ramp, handed to Windows only when it changes.
void Screen::ApplyRamp(const ScreenPlan::Ramp& ramp)
{
    if (ramp.key == toneKey) return;
    toneKey = ramp.key;
    if (ramp.kind == ScreenPlan::Ramp::Kind::None) tone.Off();
    else tone.SetCurve(ramp);
}

// The colour filter or the Smart (GPU) filter, the Smart layer, and grain.
void Screen::ApplyEngines(const Settings& v, const ScreenPlan::Engines& e, ScreenPlan::Gains gains, float bright)
{
    if (e.smart) {
        filter.Off();
        smart.Apply(SmartParams(bright), window, messages.toneFailed);
        colourGain = 1;
    } else {
        smart.Stop();
        filter.Apply(v, night, gains.prescale, gains.onScreen);
        colourGain = e.on ? gains.prescale * gains.onScreen : 1.0f;
    }
    float dpi = GetDpiForSystem() / 96.0f;
    bool bakeGrain = ScreenPlan::BakesGrain(v, e);
    if (e.layer) {
        SmartLayer::Params p;
        p.level = v.Level();
        p.gain = colourGain;
        p.grain = bakeGrain;
        p.grainAlpha = static_cast<int>(std::lround(v.GrainStrength * 1.28));
        p.grainTone = v.GrainTone;
        p.grainBlock = v.GrainCoarse ? std::max(2, static_cast<int>(std::lround(2 * dpi))) : 1;
        smartLayer.Apply(p, window, messages.toneFailed);
    } else smartLayer.Stop();
    grain.Apply(v, dpi, bakeGrain);
}

void Screen::Reset()
{
    // Built again from scratch: the displays (and their sizes) may have changed.
    smart.Stop();
    smartLayer.Stop();
    filter.Off();
    tone.Reset();
    toneKey.clear();
    grain.Invalidate();
}

void Screen::Stop()
{
    smart.Stop();
    smartLayer.Stop();
    lut.Stop();
    grain.Close();
    tone.Off();
    filter.Off();
}

// Each second: the night schedule moves on (or a preview of it ends), Night light may have changed, the engines are
// looked after, and the app in front may have a rule.
void Screen::Tick()
{
    UpdateLocation();
    NightLightTick();
    if (std::abs(NightShown() - night) > 0.004f) hooks.apply();
    if (tone.Active() && ++verifyTick % 5 == 0) tone.Verify();
    lut.Poll();
    smart.KeepOnTop();
    smartLayer.KeepOnTop();
    CheckRules();
}

// ---- Tone ----

bool Screen::EffectsAvailable() const
{
    return filter.CanShowEffects() && !(s.ToneEnabled && s.ToneMode == Settings::ToneSmartGpu && !smart.Failed());
}

void Screen::SetToneMode(int mode)
{
    s.ToneMode = mode;
    if (mode != 0 && s.ComfortLevel == Settings::CustomLevel) s.ComfortLevel = 1;   // custom is standard-only
    smart.ClearFailure();
    smartLayer.ClearFailure();
    lut.ClearFailure();
    hooks.changed(true);
}

// Writes the table for the current strength (or the do-nothing one the lab compares against) if it isn't the one
// DesktopLUT was last given.
bool Screen::WriteLut()
{
    int level = labCompare ? -1 : s.Level();
    std::wstring key = level < 0 ? L"plain" : ToneCurve::Levels[level];
    std::wstring path = LutFiles::CubePath();
    if (key == lutKey && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return false;
    if (!LutFiles::WriteLevel(path, level)) return false;
    lutKey = key;
    return true;
}

void Screen::RetestLut()
{
    lut.Stop();
    lut.ClearFailure();
    if (s.ToneMode == Settings::ToneDesktopLut) {
        s.FilterEnabled = s.ToneEnabled = true;   // the check is meaningless with tone off
        WriteLut();
        lut.Ensure(s, true);
    }
    hooks.changed(true);
}

std::wstring Screen::ComfortStatus() const
{
    if (tone.Refused()) return Lang::T(L"Windows won't accept a gamma curve here (HDR may be on), so only the even dimming is active.");
    std::wstring white = std::to_wstring(std::lround(comfort.White() * 100));
    return Lang::F(comfort.Adapting()
                       ? L"Whites at {0:0}% right now, a little lower because the screen is mostly bright.\nDark tones go deeper; bright shades spread apart, so text and colours stay clear."
                       : L"Whites at {0:0}% right now.\nDark tones go deeper; bright shades spread apart, so text and colours stay clear.",
                   {white});
}

SmartGpu::Params Screen::SmartParams(float bright) const
{
    ToneCurve::SmartParams level = ToneCurve::ForLevel(s.Level());
    SmartGpu::Params p;
    p.compress = level.compress;
    p.expand = level.expand;
    p.dimStops = level.dimStops;
    ColorFilter::Scale(shown, night, bright, p.r, p.g, p.b);   // (a rule's mode, if one applies)
    p.invert = s.Invert;
    return p;
}

void Screen::LabBegin()
{
    labSaved = s;
    s.FilterEnabled = s.ToneEnabled = true;
    if (s.CustomTone()) s.ComfortLevel = 1;
    hooks.changed(true);
}

// Hold to see the screen without tone (DesktopLUT keeps running, with a table that changes nothing).
void Screen::LabCompare(bool without)
{
    labCompare = without;
    hooks.apply();
}

void Screen::LabEnd()
{
    labCompare = false;
    if (!labSaved) return;
    LabForget();
    SetToneMode(s.ToneMode);   // clears engine failures from the lab and reapplies
}

bool Screen::LabForget()
{
    labCompare = false;
    if (!labSaved) return false;
    std::wstring confirmed = s.LutConfirmedBuild;   // a DesktopLUT check passed in the lab still counts
    s = *labSaved;
    s.LutConfirmedBuild = confirmed;
    labSaved.reset();
    return true;
}

// ---- The night schedule ----

float Screen::NightNow() const
{
    auto now = Clock::now();
    if (now < previewNightUntil) return 1;   // a moment's preview of the night strength
    Settings v = Scheduled();
    if (!v.ScheduleEnabled) return 0;
    if (now < previewUntil) return 0;
    return Night::Factor(v, LocalNow());
}

float Screen::NightShown() const { return ruleMode >= 0 ? 0 : NightNow(); }

// While a blue light slider is being moved, show what it sets, whatever the time of day: the daytime one for a few
// seconds; the schedule's night one only for a moment, then back to the daytime one until night comes.
void Screen::PreviewNight()
{
    previewUntil = Clock::now() + std::chrono::seconds(5);
    previewNightUntil = {};
}

void Screen::PreviewNightStrength() { previewNightUntil = Clock::now() + std::chrono::milliseconds(1200); }

// Sunset to sunrise needs to know where the PC is: ask Windows location now and then (a few times a day is plenty),
// keeping the last answer when location is off.
void Screen::UpdateLocation()
{
    auto now = Clock::now();
    if (!s.ScheduleEnabled || s.ScheduleMode != 0 || s.CityManual) return;
    if (locatedAt != Clock::time_point{} && now - locatedAt < std::chrono::hours(6)) return;
    locatedAt = now;
    Location::Locate(window, messages.located);
}

// The blue light filter in effect right now (0 when the filter is off, or while a colour effect takes its place;
// Windows Night light's while that is warming the screen).
float Screen::WarmthNow() const
{
    if (NightLightWarming()) return NightLight::BlueFor(nightLight.kelvin) / 100.0f;   // Night light's warmth instead
    const Settings& v = shown;
    return v.FilterEnabled && !effectShown ? static_cast<float>(Night::Blue(v, NightShown()) / 100) : 0.0f;
}

std::wstring Screen::NightStatus() const { return Night::Status(Scheduled(), LocalNow()); }

// The line under the schedule's status: still syncing, synced (with Windows location or the time zone's city), failed
// (neither works: a city has to be chosen), or a city you chose.
SettingsHost::Sync Screen::SyncState(std::wstring& text) const
{
    using Sync = SettingsHost::Sync;
    bool windows;
    std::wstring place = Night::Place(s, TimeZoneId(), windows);
    if (s.CityManual && !place.empty()) {
        text = Lang::F(L"Sunset and sunrise for {0}, the city you chose", {place});
        return Sync::CityChosen;
    }
    if (syncedAt == Clock::time_point{} && !s.CityManual && !s.ScheduleEnabled && !place.empty()) {
        text = Lang::F(L"Sunset and sunrise for {0}, from your time zone", {place});   // (off, it doesn't look you up)
        return Sync::Synced;
    }
    if (syncedAt == Clock::time_point{} && !s.CityManual) {
        text = Lang::T(L"Finding your location for sunset and sunrise...");
        return Sync::Syncing;
    }
    if (windows) {
        text = Lang::T(L"Sunset and sunrise for your location in Windows");
        return Sync::Synced;
    }
    if (!place.empty()) {
        text = Lang::F(L"Sunset and sunrise for {0}, from your time zone", {place});
        return Sync::Synced;
    }
    text = Lang::T(L"Couldn't find your location for sunset and sunrise.");
    return Sync::Failed;
}

void Screen::SetCity(const std::wstring& city)
{
    s.CityManual = !city.empty();
    s.City = city;
    syncedAt = locatedAt = {};   // automatic again: looked up on the next tick
    hooks.changed(true);
}

void Screen::Located(double lat, double lon, bool ok)
{
    syncedAt = Clock::now();   // whichever answered: Windows location, or (without it) the time zone
    if (!ok || s.CityManual) return;
    std::wstring where = Night::Coordinates(lat, lon);
    if (where == s.City) return;
    s.City = where;
    hooks.changed(true);
}

Settings Screen::Effective() const
{
    Settings v = Scheduled();
    if (ruleMode >= 0) Modes::Apply(v, ruleMode);
    if (NightLightWarming()) v.BlueFilter = v.NightBlue = 0;   // Night light does the warming: no second layer of it
    return v;
}

// ---- Windows Night light ----

// The settings with the night schedule as it runs: Windows Night light's hours and strength while it has a schedule
// and BlinkBuddy follows it (the schedule's own settings stay as set, for when it doesn't).
Settings Screen::Scheduled() const
{
    Settings v = s;
    if (!FollowsNightLight()) return v;
    v.ScheduleEnabled = true;
    v.ScheduleMode = 1;
    v.NightFrom = nightLight.Start();
    v.NightTo = nightLight.End();
    v.NightBlue = NightLight::BlueFor(nightLight.kelvin);
    v.NightFade = 0;   // (Night light switches at those times)
    return v;
}

bool Screen::FollowsNightLight() const { return s.NightLightSync && nightLight.found && nightLight.scheduled; }
bool Screen::NightLightWarming() const { return s.NightLightSync && nightLight.found && nightLight.on; }

// Each second: Night light read again, in case it changed (in Windows Settings, or its schedule switched it).
// Windows sends no notice of these changes; two small registry reads a second cost next to nothing.
void Screen::NightLightTick()
{
    NightLight::State now = NightLight::Read();
    if (now == nightLight) return;
    nightLight = now;
    hooks.apply();
    hooks.syncSettings();
}

// ---- Per-app rules ----

// Is an app with a rule in front? Checked the moment another window comes to the front (and each second, to be
// sure). A window that only passes through - the taskbar, Start, Alt+Tab, BlinkBuddy itself - leaves the app's mode
// on while the app's window is still there to see.
void Screen::CheckRules()
{
    if ((s.AppRules.empty() || !s.RulesEnabled) && ruleMode < 0) return;
    int mode;
    std::wstring front;
    HWND frontWindow;
    if (!s.AdvancedView || !s.RulesEnabled) mode = -1;   // (switched off, or an Advanced feature in Simple: kept, but idle)
    else if (ForegroundApp::Get(front, frontWindow)) {
        mode = Modes::RuleFor(s, front);
        ruleWindow = mode >= 0 ? frontWindow : nullptr;
    } else
        mode = ruleMode >= 0 && ForegroundApp::StillShown(ruleWindow) ? ruleMode : -1;
    if (mode == ruleMode) return;
    ruleMode = mode;
    hooks.apply();
}

} // namespace bb
