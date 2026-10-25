#include "app/App.h"
#include "core/Lang.h"
#include "core/Modes.h"
#include "features/screen/Backlight.h"
#include "platform/Activity.h"
#include "platform/DarkMode.h"
#include "platform/KeepAwake.h"
#include "platform/Location.h"
#include "ui/Theme.h"
#include "resource.h"
#include <commctrl.h>
#include <wtsapi32.h>
#include <memory>

#pragma comment(lib, "wtsapi32.lib")

namespace bb {
namespace {

constexpr UINT_PTR TickTimer = 1;
constexpr wchar_t WindowClass[] = L"BlinkBuddy.App";

// Menu commands.
enum : UINT {
    CmdSettings = 1, CmdBreakNow, CmdPause30, CmdPause60, CmdPauseUntilResumed, CmdResume, CmdExit,
    CmdFilter, CmdAwakeOff, CmdAwake1h, CmdAwake2h, CmdAwakeOn,
    CmdMode,   // CmdMode + mode, for each screen mode
};

// "4:05": minutes and seconds.
std::wstring MinSec(int seconds)
{
    seconds = seconds < 0 ? 0 : seconds;
    wchar_t text[16];
    swprintf_s(text, L"%d:%02d", seconds / 60, seconds % 60);
    return text;
}

// Gives memory pages back to Windows after a break (the next one is a while away).
void TrimMemory() { SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1)); }

} // namespace

App::App(HINSTANCE instance, HANDLE showRequest, bool openSettings, bool offscreen)
    : instance(instance), showRequest(showRequest), settings(Settings::Load()), offscreen(offscreen)
{
    Lang::Use(settings.Language);
    Theme::UseAccent(settings);
    DarkMode::UseForMenus();

    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = WindowClass;
    RegisterClassExW(&wc);
    // A hidden top-level window (not message-only: it must hear Explorer's "TaskbarCreated" broadcast).
    window = CreateWindowExW(0, WindowClass, L"BlinkBuddy", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, this);

    LoadIconWithScaleDown(instance, MAKEINTRESOURCEW(IDI_APP), GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), &icon);
    LoadIconWithScaleDown(instance, MAKEINTRESOURCEW(IDI_APP), GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), &bigIcon);
    tray.Add(window, icon, L"BlinkBuddy");
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    KeepAwake::Set(settings.KeepAwake);
    frontWatch = std::make_unique<FrontWatch>(window, FrontChangedMessage);
    WTSRegisterSessionNotification(window, NOTIFY_FOR_THIS_SESSION);   // to reapply the filter after unlocking
    ApplyScreen();
    if (settings.HardwareBrightness) {
        probing = Probe::Refresh;
        Backlight::Probe(window, ProbedMessage);
    }

    SetTimer(window, TickTimer, 1000, nullptr);
    if (openSettings) ShowSettings();
}

App::~App()
{
    settingsWindow.reset();
    frontWatch.reset();
    WTSUnRegisterSessionNotification(window);
    filter.Off();
    KeepAwake::Set(false);
    card.reset();
    tray.Remove();
    if (window) DestroyWindow(window);
    if (icon) DestroyIcon(icon);
    if (bigIcon) DestroyIcon(bigIcon);
}

int App::Run()
{
    // Waits for window messages or a second copy asking for the settings, whichever comes first: no polling.
    for (;;) {
        DWORD woke = MsgWaitForMultipleObjectsEx(1, &showRequest, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (woke == WAIT_OBJECT_0) ShowSettings();
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return static_cast<int>(msg.wParam);
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

void App::Changed(bool screen)
{
    dirty = true;
    reminders.BlinkIntervalChanged();
    if (screen) ApplyScreen();
}

LRESULT CALLBACK App::WindowProc(HWND window, UINT message, WPARAM w, LPARAM l)
{
    if (message == WM_NCCREATE) {
        // (The handle is kept now, before CreateWindowEx returns: Handle passes it on to DefWindowProc from here on.)
        auto app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        app->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    auto app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    return app ? app->Handle(message, w, l) : DefWindowProcW(window, message, w, l);
}

LRESULT App::Handle(UINT message, WPARAM w, LPARAM l)
{
    switch (message) {
    case WM_TIMER:
        if (w == TickTimer) OnTick();
        return 0;
    case Tray::CallbackMessage:
        switch (LOWORD(l)) {
        case WM_CONTEXTMENU: ShowMenu(); break;
        case WM_MOUSEMOVE: UpdateTrayTip(); break;   // the tip is made when it is about to show
        case NIN_SELECT:
        case NIN_KEYSELECT: ShowSettings(); break;
        }
        return 0;
    case BreakCard::DoneMessage:
        BreakDone(w != 0, static_cast<int>(l));
        return 0;
    case FrontChangedMessage:
        CheckRules();
        return 0;
    case LocatedMessage: {
        std::unique_ptr<Location::Fix> fix(reinterpret_cast<Location::Fix*>(l));
        Located(fix->lat, fix->lon, fix->ok);
        return 0;
    }
    case ProbedMessage:
        Probed(LOWORD(w), HIWORD(w), static_cast<int>(l));
        return 0;
    case QuitMessage:
        Exit();
        return 0;
    case RebuildMessage: {
        if (!settingsWindow) return 0;
        POINT at = settingsWindow->Position();
        settingsWindow.reset();
        ui::Fonts::Release();   // (the language may need another font)
        ShowSettings(&at, w != 0);
        return 0;
    }
    case WM_DISPLAYCHANGE:
        ReapplyScreen();
        return 0;
    case WM_WTSSESSION_CHANGE:
        if (w == WTS_SESSION_UNLOCK) ReapplyScreen();
        return 0;
    }
    if (message == taskbarCreated && taskbarCreated != 0) {
        tray.AddAgain();
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}

// Once a second: save what changed, keep the screen up to date, and show whatever reminder is due.
void App::OnTick()
{
    if (dirty) {
        dirty = false;
        settings.Save();
    }
    ScreenTick();
    if (card || Paused()) return;
    bool fullscreen = settings.PauseInFullscreen && FullscreenAppRunning();
    switch (reminders.Tick(IdleSeconds(), fullscreen)) {
    case Reminders::Due::Blink: Blink(false); break;
    case Reminders::Due::LookAway: StartBreak(false); break;
    case Reminders::Due::StandUp: StartBreak(true); break;
    case Reminders::Due::Nothing: break;
    }
}

void App::Blink(bool instant)
{
    // (The eyelids style - closing over the whole screen - arrives with the guided breaks, chunk 7.)
    eye.Play(settings, instant);
}

void App::StartBreak(bool standUp)
{
    if (card) return;
    // (The guided eye exercises and the stretch coach arrive in chunk 7; until then every break is a card.)
    card = std::make_unique<BreakCard>(window, standUp, settings);
}

void App::BreakDone(bool standUp, int snoozeSeconds)
{
    card.reset();
    reminders.BreakFinished(standUp, snoozeSeconds);
    TrimMemory();
}

std::wstring App::BreakStatus() const
{
    if (Paused()) {
        if (pausedUntil == Clock::time_point::max()) return Lang::T(L"Reminders paused");
        // The time it resumes, as Windows writes times here.
        SYSTEMTIME st{};
        FILETIME ft{};
        auto ticks = std::chrono::duration_cast<std::chrono::seconds>(pausedUntil.time_since_epoch()).count();
        ULARGE_INTEGER u{};
        u.QuadPart = (ticks + 11644473600LL) * 10000000LL;   // Unix seconds -> FILETIME
        ft.dwLowDateTime = u.LowPart;
        ft.dwHighDateTime = u.HighPart;
        FILETIME local{};
        FileTimeToLocalFileTime(&ft, &local);
        FileTimeToSystemTime(&local, &st);
        wchar_t time[32] = {};
        GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, time, 32);
        return Lang::F(L"Paused until {0}", {time});
    }
    if (!settings.BreakEnabled) return Lang::T(L"Breaks are off");
    return Lang::F(L"Next break in {0}", {MinSec(reminders.SecondsToBreak())});
}

std::wstring App::StandUpStatus() const
{
    if (Paused()) return Lang::T(L"Reminders paused");
    if (!settings.LongBreakEnabled) return Lang::T(L"Stand-up reminders are off");
    return Lang::F(L"Next stand-up in {0}", {MinSec(reminders.SecondsToStandUp())});
}

void App::UpdateTrayTip()
{
    std::wstring tip = L"BlinkBuddy\n" + BreakStatus();
    if (settings.LongBreakEnabled) tip += L"\n" + StandUpStatus();
    tray.SetTip(tip);
}

void App::ShowMenu()
{
    auto checked = [](bool on) -> UINT { return on ? MF_CHECKED : MF_UNCHECKED; };
    HMENU menu = CreatePopupMenu();
    HMENU pause = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, CmdSettings, Lang::T(L"Settings").c_str());
    SetMenuDefaultItem(menu, CmdSettings, FALSE);
    AppendMenuW(menu, MF_STRING | (card ? MF_GRAYED : 0), CmdBreakNow, Lang::T(L"Take a break now").c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // Screen modes, the filter, and keeping the PC awake (for a while, or until switched off).
    HMENU modes = CreatePopupMenu();
    for (int m = 0; m < Modes::Count; m++)
        AppendMenuW(modes, MF_STRING | checked(settings.Mode == m), CmdMode + m, Lang::T(Modes::Names[m]).c_str());
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(modes), Lang::T(L"Screen mode").c_str());
    AppendMenuW(menu, MF_STRING | checked(settings.FilterEnabled), CmdFilter, Lang::T(L"Screen filter").c_str());
    HMENU awake = CreatePopupMenu();
    AppendMenuW(awake, MF_STRING | checked(!KeepingAwake()), CmdAwakeOff, Lang::T(L"Off").c_str());
    AppendMenuW(awake, MF_STRING, CmdAwake1h, Lang::T(L"For 1 hour").c_str());
    AppendMenuW(awake, MF_STRING, CmdAwake2h, Lang::T(L"For 2 hours").c_str());
    AppendMenuW(awake, MF_STRING | checked(settings.KeepAwake), CmdAwakeOn, Lang::T(L"Until I turn it off").c_str());
    AppendMenuW(menu, MF_POPUP | checked(KeepingAwake()), reinterpret_cast<UINT_PTR>(awake), Lang::T(L"Keep PC awake").c_str());

    if (Paused()) {
        AppendMenuW(menu, MF_STRING, CmdResume, Lang::T(L"Resume reminders").c_str());
    } else {
        AppendMenuW(pause, MF_STRING, CmdPause30, Lang::T(L"For 30 minutes").c_str());
        AppendMenuW(pause, MF_STRING, CmdPause60, Lang::T(L"For 1 hour").c_str());
        AppendMenuW(pause, MF_STRING, CmdPauseUntilResumed, Lang::T(L"Until I resume").c_str());
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(pause), Lang::T(L"Pause reminders").c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CmdExit, Lang::T(L"Exit").c_str());

    // The window must be in front for the menu to close when you click elsewhere.
    SetForegroundWindow(window);
    POINT at = Tray::MenuPoint();
    UINT command = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, at.x, at.y, window, nullptr);
    PostMessageW(window, WM_NULL, 0, 0);
    if (Paused()) DestroyMenu(pause);   // (not attached to the menu then)
    DestroyMenu(menu);

    switch (command) {
    case CmdSettings: ShowSettings(); break;
    case CmdBreakNow: StartBreak(false); break;
    case CmdPause30: Pause(30); break;
    case CmdPause60: Pause(60); break;
    case CmdPauseUntilResumed: Pause(-1); break;
    case CmdResume: pausedUntil = {}; break;
    case CmdExit: Exit(); break;
    case CmdFilter: ToggleFilter(); break;
    case CmdAwakeOff: SetKeepAwake(false); break;
    case CmdAwake1h: KeepAwakeFor(60); break;
    case CmdAwake2h: KeepAwakeFor(120); break;
    case CmdAwakeOn: SetKeepAwake(true); break;
    default:
        if (command >= CmdMode && command - CmdMode < static_cast<UINT>(Modes::Count)) ChooseMode(static_cast<int>(command - CmdMode));
        break;
    }
}

void App::Pause(int minutes)
{
    pausedUntil = minutes < 0 ? Clock::time_point::max() : Clock::now() + std::chrono::minutes(minutes);
}

bool App::Paused() const { return Clock::now() < pausedUntil; }

void App::Exit()
{
    KillTimer(window, TickTimer);
    settingsWindow.reset();
    if (dirty) settings.Save();
    card.reset();
    tray.Remove();
    PostQuitMessage(0);
}

} // namespace bb
