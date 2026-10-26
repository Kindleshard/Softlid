// The screen side of the app: the pipeline (app/Screen) plus what lies outside it - the real backlight, the reading
// strip, screen modes, and keeping the PC awake.
#include "app/App.h"
#include "core/Lang.h"
#include "core/Modes.h"
#include "features/screen/Backlight.h"
#include "platform/KeepAwake.h"
#include <algorithm>

namespace bb {

void App::ApplyScreen()
{
    screen->Apply();
    ApplyExtras(screen->Shown());
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
    screen->Reset();   // the displays (and their sizes) may have changed
    ApplyScreen();
}

// Each second: the screen pipeline's round, and a timed keep-awake may be over.
void App::ScreenTick()
{
    screen->Tick();
    if (awakeUntil != Clock::time_point{} && Clock::now() >= awakeUntil) {
        awakeUntil = {};
        KeepAwake::Set(settings.KeepAwake);
    }
}

// ---- What the settings, mini mode and the tone lab ask of the screen ----

bool App::EffectsAvailable() const { return screen->EffectsAvailable(); }
void App::SetToneMode(int mode) { screen->SetToneMode(mode); }
void App::RetestLut() { screen->RetestLut(); }
void App::LabCompare(bool without) { screen->LabCompare(without); }
double App::ComfortNet(double x) const { return screen->ComfortNet(x); }
std::wstring App::ComfortStatus() const { return screen->ComfortStatus(); }
const std::wstring& App::ToneStatus() const { return screen->ToneStatus(); }
std::wstring App::SmartStatus() const { return screen->SmartStatus(); }
std::wstring App::LayerStatus() const { return screen->LayerStatus(); }
const std::wstring& App::LutStatus() const { return screen->LutStatus(); }
bool App::LutFailed() const { return screen->LutFailed(); }
const std::wstring& App::GuardNote() const { return screen->GuardNote(); }
void App::PreviewNight() { screen->PreviewNight(); }
void App::PreviewNightStrength() { screen->PreviewNightStrength(); }
int App::RuleMode() const { return screen->RuleMode(); }
float App::WarmthNow() const { return screen->WarmthNow(); }
std::wstring App::NightStatus() const { return screen->NightStatus(); }
App::Sync App::SyncState(std::wstring& text) const { return screen->SyncState(text); }
void App::SetCity(const std::wstring& city) { screen->SetCity(city); }
const NightLight::State& App::NightLightState() const { return screen->NightLightState(); }
bool App::FollowsNightLight() const { return screen->FollowsNightLight(); }
bool App::NightLightWarming() const { return screen->NightLightWarming(); }
Settings App::Scheduled() const { return screen->Scheduled(); }

// The lab takes the settings window's place, which reopens afresh when the lab closes.
void App::OpenToneLab() { PostMessageW(window, LabOpenMessage, 0, 0); }   // (not from inside the settings' own click)

// ---- Modes ----

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
