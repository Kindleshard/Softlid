// The app's side of the settings window: opening and closing it, building it again, and what its Options page asks
// for (language, accent, view, starting with Windows, resetting everything, updates).
#include "app/App.h"
#include "core/Lang.h"
#include <cstdio>
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
    if (s.ToneMode != 0) SetToneMode(0);   // the engines are Advanced too: Standard takes over
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

// ---- Updates ----

namespace bb {

void App::CheckForUpdates()
{
    if (update.busy) return;
    update.busy = true;
    update.look = UpdateState::Look::Plain;
    update.text = Lang::T(L"Checking...");
    quietCheck = false;
    Updater::Check(window, UpdateMessage);
}

void App::InstallUpdate()
{
    if (update.busy || !update.release) return;
    update.busy = update.downloading = true;
    update.progress = 0;
    Updater::Install(*update.release, window, UpdateMessage);
}

// Once a week (if allowed), a quiet look for a newer version; only a newer one is mentioned, from the tray.
void App::WeeklyUpdateCheck()
{
    if (!settings.UpdateCheck || !Updater::Available() || update.busy || update.release) return;
    SYSTEMTIME today;
    GetLocalTime(&today);
    int y = 0, m = 0, d = 0;
    if (swscanf_s(settings.LastUpdateCheck.c_str(), L"%d-%d-%d", &y, &m, &d) == 3) {
        SYSTEMTIME last{static_cast<WORD>(y), static_cast<WORD>(m), 0, static_cast<WORD>(d)};
        FILETIME a, b;
        if (SystemTimeToFileTime(&last, &a) && SystemTimeToFileTime(&today, &b)) {
            ULONGLONG days = (ULARGE_INTEGER{b.dwLowDateTime, b.dwHighDateTime}.QuadPart -
                              ULARGE_INTEGER{a.dwLowDateTime, a.dwHighDateTime}.QuadPart) / (10000000ULL * 86400);
            if (days < 7) return;
        }
    }
    update.busy = quietCheck = true;
    Updater::Check(window, UpdateMessage);
}

void App::Updated(const Updater::Result& r)
{
    using Kind = Updater::Result::Kind;
    using Look = UpdateState::Look;
    switch (r.kind) {
    case Kind::Progress:
        update.progress = r.progress;
        update.look = Look::Newer;
        update.text = r.text;
        break;
    case Kind::Ready:
        update.text = r.text;
        Updater::StartNewCopy();   // (it waits for this copy to quit)
        Exit();
        return;
    case Kind::Failed:
        update.busy = update.downloading = false;
        update.look = Look::Plain;
        update.text = r.text;
        break;
    default:   // the end of a check
        update.busy = false;
        if (quietCheck) {
            SYSTEMTIME today;
            GetLocalTime(&today);
            wchar_t date[16];
            swprintf_s(date, L"%04d-%02d-%02d", today.wYear, today.wMonth, today.wDay);
            settings.LastUpdateCheck = date;
            dirty = true;
        }
        if (r.kind == Kind::Newer) {
            update.release = r.release;
            update.look = Look::Newer;
            update.text = Lang::F(L"Update available: version {0}", {r.release.tag});
            if (quietCheck) tray.Notify(Lang::F(L"BlinkBuddy {0} is available", {r.release.tag}), Lang::T(L"Open BlinkBuddy options to install it."));
        } else if (!quietCheck) {
            update.look = r.kind == Kind::UpToDate ? Look::UpToDate : Look::Plain;
            update.text = r.text;
        }
        quietCheck = false;
    }
    if (settingsWindow) settingsWindow->RefreshStatus();
}

} // namespace bb
