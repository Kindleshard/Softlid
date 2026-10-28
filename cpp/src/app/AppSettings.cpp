// The app's side of the settings window: opening and closing it, building it again, and what its Options page asks
// for (language, accent, view, starting with Windows, resetting everything, updates).
#include "app/App.h"
#include "core/Lang.h"
#include "features/welcome/Tour.h"
#include "platform/Clock.h"
#include "platform/Files.h"
#include "platform/Memory.h"
#include "platform/MemJournal.h"
#include <cstdio>
#include "ui/Theme.h"

namespace bb {
namespace {

constexpr wchar_t RunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

} // namespace

void App::ShowSettings(const POINT* at, bool optionsPage)
{
    if (mini) {
        // Opening the settings (tray, a second start) unfolds mini mode.
        settings.MiniMode = false;
        Changed();
        mini.reset();
    }
    if (settingsWindow) {
        HWND h = settingsWindow->Handle();
        if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
        SetForegroundWindow(h);
        return;
    }
    settingsWindow = std::make_unique<SettingsWindow>(*this, settings, offscreen);
    MemJournal::Note(L"settings opened");
    settingsWindow->onClosed = [this] { SettingsClosed(); };
    POINT away{-30000, 0};
    settingsWindow->Show(offscreen && !CloakedCopy() ? &away : at, optionsPage, bigIcon, !offscreen);
}

// The window is gone (closed with its button): all it held goes back to Windows.
void App::SettingsClosed()
{
    probeDone = nullptr;
    if (dirty) {
        dirty = false;
        settings.Save();
    }
    // (This runs inside the window's own closing: it is freed once that is over, and then its memory goes back.)
    DeleteLater(std::move(settingsWindow), [] {
        ui::Fonts::Release();
        TrimMemory();
        MemJournal::Note(L"settings closed, trimmed");
    });
}

void App::Rebuild(bool optionsPage) { PostMessageW(window, RebuildMessage, optionsPage, 0); }

void App::Quit()
{
    if (settingsWindow) ShowWindow(settingsWindow->Handle(), SW_HIDE);
    PostMessageW(window, QuitMessage, 0, 0);   // (not from inside the window's own click)
}

void App::ShowTour() { Tour::Open(*this); }

void App::SetLanguage(const std::wstring& code)
{
    settings.Language = code;
    Changed();
    Lang::Use(code);
    Rebuild(true);   // (the tray menu is made afresh each time it opens)
}

void App::SetAccent(bool fromWindows, int color)
{
    settings.AccentFromWindows = fromWindows;
    settings.AccentColor = color;
    Changed();
    Theme::UseAccent(settings);
    Rebuild(true);
}

void App::UseCompactView()
{
    Settings& s = settings;
    s.AdvancedView = false;
    s.Invert = false;
    s.CoolRange = false;   // cooler than daylight is an advanced setting too
    s.ScheduleMode = 0;    // and so are custom night hours: Simple follows sunset and sunrise
    s.BlueFilter = std::max(0, s.BlueFilter);
    s.KeptBlue = std::max(0, s.KeptBlue);
    s.GrainEnabled = false;
    if (s.CustomTone()) s.ComfortLevel = 1;
    else if (s.ComfortLevel == 3) s.ComfortLevel = 2;   // Max: the strongest compact step
    // What lives under "More options" (which Simple doesn't show) and would otherwise go on working unseen:
    s.GrayStyle = 0;
    s.NightFade = 60;
    s.ForceRest = false;
    s.StripFollow = true;   // (moving it with the keys, or dimming one side only, would be a mystery)
    s.StripCover = 0;
    if (s.KeepAwake || KeepingAwake()) SetKeepAwake(false);
    // (Modes for apps stay listed, but only apply in the Advanced view: see CheckRules.)
    if (s.ToneMode != Settings::ToneStandard) SetToneMode(Settings::ToneStandard);   // the engines are Advanced too: Standard takes over
    else Changed(true);
}

void App::ResetAll()
{
    if (settings.HardwareBrightness) SetHardwareBrightness(false, nullptr);
    settings = Settings{};
    reminders.Reset();
    Theme::UseAccent(settings);
    Changed(true);
    Rebuild(false);
}

// On only when the entry starts this copy: one left by another (the C# version, a copy in another folder) would start
// that one at sign-in instead, so it shows as off, and switching it on points it here.
bool App::RunAtStartup() const
{
    wchar_t value[2048];
    DWORD size = sizeof value;
    if (RegGetValueW(HKEY_CURRENT_USER, RunKey, L"BlinkBuddy", RRF_RT_REG_SZ, nullptr, value, &size) != ERROR_SUCCESS) return false;
    std::wstring here = L"\"" + ExePath() + L"\"";
    size_t length = wcslen(value);
    return length >= here.size() &&
           CompareStringOrdinal(value, static_cast<int>(here.size()), here.c_str(), static_cast<int>(here.size()), TRUE) == CSTR_EQUAL;
}

void App::SetRunAtStartup(bool on)
{
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, RunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
    if (on) {
        std::wstring command = L"\"" + ExePath() + L"\" --tray";
        RegSetValueExW(key, L"BlinkBuddy", 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                       static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, L"BlinkBuddy");
    }
    RegCloseKey(key);
}

void App::SetMiniEnabled(bool on)
{
    settings.MiniEnabled = on;
    Changed();
    if (on) return;
    settings.MiniMode = false;   // off: it never appears (and closes if it was showing)
    mini.reset();
}

} // namespace bb


namespace bb {

// Once a week (if allowed), a quiet look for a newer version; only a newer one is mentioned, from the tray.
void App::WeeklyUpdateCheck()
{
    if (Updater::Available()) updates.Weekly(settings, LocalNow());
}

} // namespace bb
