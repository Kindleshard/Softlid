// The app's side of the settings window: opening and closing it, building it again, and what its Options page asks
// for (language, accent, view, starting with Windows, resetting everything).
#include "app/App.h"
#include "core/Lang.h"
#include "ui/Theme.h"

namespace bb {
namespace {

constexpr wchar_t RunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

void TrimMemory()
{
    HeapCompact(GetProcessHeap(), 0);   // free blocks at the heap's end go back to Windows
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
}

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
    settingsWindow->onClosed = [this] { SettingsClosed(); };
    POINT away{-30000, 0};
    settingsWindow->Show(offscreen ? &away : at, optionsPage, bigIcon, !offscreen);
}

// The window is gone (closed with its button): all it held goes back to Windows.
void App::SettingsClosed()
{
    probeDone = nullptr;
    settingsWindow.reset();   // (allowed from its own onClosed)
    ui::Fonts::Release();
    if (dirty) {
        dirty = false;
        settings.Save();
    }
    TrimMemory();
}

void App::Rebuild(bool optionsPage) { PostMessageW(window, RebuildMessage, optionsPage, 0); }

void App::Quit()
{
    if (settingsWindow) ShowWindow(settingsWindow->Handle(), SW_HIDE);
    PostMessageW(window, QuitMessage, 0, 0);   // (not from inside the window's own click)
}

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
    s.ToneMode = 0;   // the engines are Advanced too: Standard takes over
    Changed(true);
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

bool App::RunAtStartup() const
{
    return RegGetValueW(HKEY_CURRENT_USER, RunKey, L"BlinkBuddy", RRF_RT_REG_SZ, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
}

void App::SetRunAtStartup(bool on)
{
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, RunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
    if (on) {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring command = L"\"" + std::wstring(path) + L"\" --tray";
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
