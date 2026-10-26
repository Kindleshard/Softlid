#include "app/App.h"
#include "core/Hotkeys.h"
#include "core/Clock.h"
#include "core/Lang.h"
#include "core/Modes.h"
#include "features/breaks/GuidedBreak.h"
#include "features/breaks/StretchBreak.h"
#include "features/screen/Backlight.h"
#include "platform/Activity.h"
#include "platform/Clock.h"
#include "platform/DarkMode.h"
#include "platform/KeepAwake.h"
#include "platform/Memory.h"
#include "platform/MemJournal.h"
#include "platform/Location.h"
#include "platform/Text.h"
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
    CmdFilter, CmdStrip, CmdAwakeOff, CmdAwake1h, CmdAwake2h, CmdAwakeOn,
    CmdMode,   // CmdMode + mode, for each screen mode
};

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
    if (!offscreen) tray.Add(window, icon, L"BlinkBuddy");   // (a diagnostic copy stays out of the tray)
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    KeepAwake::Set(settings.KeepAwake);
    frontWatch = std::make_unique<FrontWatch>(window, FrontChangedMessage);
    WTSRegisterSessionNotification(window, NOTIFY_FOR_THIS_SESSION);   // to reapply the filter after unlocking
    screen = std::make_unique<Screen>(settings, window, Screen::Messages{LocatedMessage, ToneFailedMessage},
        Screen::Hooks{
            [this] { ApplyScreen(); },
            [this](bool screenToo) { Changed(screenToo); },
            [this] { if (settingsWindow) settingsWindow->SyncFromApp(); },
            [this] { if (settingsWindow) settingsWindow->RefreshStatus(); },
        });
    if (!offscreen) screen->ClearLeftover();   // (a diagnostic copy beside the real one must not undo its curve)
    ApplyScreen();
    if (settings.HardwareBrightness) {
        probing = Probe::Refresh;
        Backlight::Probe(window, ProbedMessage);
    }

    RegisterHotkeys();
    SetTimer(window, TickTimer, 1000, nullptr);
    WeeklyUpdateCheck();
    if (openSettings) ShowSettings();
    else if (settings.MiniMode && settings.MiniEnabled) ShowMini();
}

App::~App()
{
    DeleteLaterNow(false);   // (windows closed just before exit: freed, without their follow-ups)
    settingsWindow.reset();
    mini.reset();
    strip.reset();
    SuspendHotkeys();
    frontWatch.reset();
    WTSUnRegisterSessionNotification(window);
    lab.reset();
    screen->Stop();
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

void App::Changed(bool screenToo)
{
    dirty = true;
    reminders.BlinkIntervalChanged();
    if (screenToo) ApplyScreen();
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
        screen->CheckRules();
        return 0;
    case LocatedMessage: {
        auto fix = TakePosted<Location::Fix>(l);
        screen->Located(fix->lat, fix->lon, fix->ok);
        return 0;
    }
    case ProbedMessage:
        Probed(LOWORD(w), HIWORD(w), static_cast<int>(l));
        return 0;
    case LeaveMiniMessage:
        mini.reset();
        ShowSettings();
        return 0;
    case ToneFailedMessage:   // (the engine says why in its status)
        ApplyScreen();
        if (settingsWindow) settingsWindow->RefreshStatus();
        return 0;
    case LabOpenMessage:
        if (lab) {
            SetForegroundWindow(lab->Handle());
            return 0;
        }
        labReopen = settingsWindow != nullptr;
        if (settingsWindow) settingsWindow->Close();   // (its onClosed tidies up)
        screen->LabBegin();
        lab = std::make_unique<ToneLab>(*this);
        lab->onClosed = [this] {   // settings back as they were, and the settings window again if it was open
            DeleteLater(std::move(lab), [this] {
                screen->LabEnd();
                if (labReopen) ShowSettings();
                else TrimMemory();
            });
        };
        lab->Start(bigIcon);
        return 0;
    case UpdateMessage: {
        Updates::Effects fx = updates.Received(*TakePosted<Updater::Result>(l), settings, LocalNow());
        if (fx.saveSettings) dirty = true;
        if (!fx.notifyTitle.empty()) tray.Notify(fx.notifyTitle, fx.notifyText);
        if (fx.restart) {
            Updater::StartNewCopy();   // (it waits for this copy to quit)
            Exit();
            return 0;
        }
        if (fx.refresh && settingsWindow) settingsWindow->RefreshStatus();
        return 0;
    }
    case WM_INPUT:
        if (strip) strip->MouseMoved();   // the mouse moved: the reading strip follows
        break;                             // (Windows tidies up the input after)
    case WM_HOTKEY:
        OnHotkey(static_cast<int>(w));
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
    MemJournal::Check();
    if (dirty) {
        dirty = false;
        settings.Save();
    }
    ScreenTick();
    if (++hourTick >= 3600) {
        hourTick = 0;
        WeeklyUpdateCheck();
    }
    if (card || Paused()) return;
    FocusTick();
    if (card) return;
    bool fullscreen = settings.PauseInFullscreen && FullscreenAppRunning();
    // While the focus timer runs, it decides when to rest instead of the look-away and stand-up reminders.
    switch (reminders.Tick(IdleSeconds(), fullscreen, focusTimer.State() == Focus::Off)) {
    case Reminders::Due::Blink: Blink(false); break;
    case Reminders::Due::LookAway: StartBreak(false); break;
    case Reminders::Due::StandUp: StartBreak(true); break;
    case Reminders::Due::Nothing: break;
    }
}

void App::Blink(bool instant)
{
    MemJournal::Note(L"blink reminder");
    if (settings.BlinkStyle == 1) {
        if (instant) return;   // live tweaks are for the eye icon's size, opacity and place
        if (!lids) lids = std::make_unique<Eyelids>();
        lids->Play(settings);
        return;
    }
    eye.Play(settings, instant);
}

void App::StartBreak(bool standUp)
{
    if (card) return;
    MemJournal::Note(standUp ? L"stand-up break" : L"look-away break");
    // Eye exercises and the stretch coach take the whole screen; otherwise a small card counts down.
    if (!standUp && settings.Exercises) card = std::make_unique<GuidedBreak>(window, settings, WarmthNow());
    else if (standUp && settings.StretchGuided) card = std::make_unique<StretchBreak>(window, settings);
    else card = std::make_unique<BreakCard>(window, standUp, settings);
}

void App::BreakDone(bool standUp, int snoozeSeconds)
{
    card.reset();
    if (restCard) {
        // The focus timer's rest is over (or snoozed): the next work period.
        restCard = false;
        focusTimer.RestOver(snoozeSeconds);
    } else {
        reminders.BreakFinished(standUp, snoozeSeconds);
    }
    TrimMemory();
    MemJournal::Note(L"break over, trimmed");
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
    if (focusTimer.State() != Focus::Off) tip = L"BlinkBuddy\n" + Lang::F(L"Focus timer: {0}", {FocusText()});
    tray.SetTip(tip);
}

void App::ShowMenu()
{
    MemJournal::Note(L"tray menu");
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
    // (Each with its shortcut beside it, as menus show them.)
    auto withKeys = [this](const wchar_t* english, int hotkey) {
        std::wstring keys = ReplaceAll(Hotkeys::Pretty(Hotkeys::Get(settings, hotkey)), L" ", L"");
        return Lang::T(english) + (keys.empty() ? L"" : L"	" + keys);
    };
    AppendMenuW(menu, MF_STRING | checked(settings.FilterEnabled), CmdFilter, withKeys(L"Screen filter", Hotkeys::Filter).c_str());
    AppendMenuW(menu, MF_STRING | checked(settings.StripEnabled), CmdStrip, withKeys(L"Reading strip", Hotkeys::Strip).c_str());
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
    case CmdStrip: OnHotkey(Hotkeys::Strip); break;
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
    mini.reset();
    strip.reset();
    SuspendHotkeys();
    lab.reset();
    if (screen->LabForget()) dirty = true;   // the lab's trial settings aren't kept (also when it just closed)
    screen->Stop();
    if (dirty) settings.Save();
    card.reset();
    tray.Remove();
    PostQuitMessage(0);
}

} // namespace bb
