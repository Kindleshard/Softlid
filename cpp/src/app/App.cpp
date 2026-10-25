#include "app/App.h"
#include "core/Lang.h"
#include "platform/DarkMode.h"
#include "resource.h"
#include <commctrl.h>

namespace bb {
namespace {

constexpr UINT_PTR TickTimer = 1;
constexpr wchar_t WindowClass[] = L"BlinkBuddy.App";

// Menu commands.
enum : UINT { CmdSettings = 1, CmdPause30, CmdPause60, CmdPauseUntilResumed, CmdResume, CmdExit };

} // namespace

App::App(HINSTANCE instance, HANDLE showRequest, bool openSettings)
    : instance(instance), showRequest(showRequest), settings(Settings::Load())
{
    Lang::Use(settings.Language);
    DarkMode::UseForMenus();

    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = WindowClass;
    RegisterClassExW(&wc);
    // A hidden top-level window (not message-only: it must hear Explorer's "TaskbarCreated" broadcast).
    window = CreateWindowExW(0, WindowClass, L"BlinkBuddy", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, this);

    LoadIconWithScaleDown(instance, MAKEINTRESOURCEW(IDI_APP), GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), &icon);
    tray.Add(window, icon, L"BlinkBuddy");
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    SetTimer(window, TickTimer, 1000, nullptr);
    if (openSettings) ShowSettings();
}

App::~App()
{
    tray.Remove();
    if (window) DestroyWindow(window);
    if (icon) DestroyIcon(icon);
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

void App::Changed() { dirty = true; }

LRESULT CALLBACK App::WindowProc(HWND window, UINT message, WPARAM w, LPARAM l)
{
    if (message == WM_NCCREATE)
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams));
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
        case NIN_SELECT:
        case NIN_KEYSELECT: ShowSettings(); break;
        }
        return 0;
    }
    if (message == taskbarCreated && taskbarCreated != 0) {
        tray.AddAgain();
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}

// Once a second: save what changed, and keep the tray tip current. (Reminders and the screen come in later chunks.)
void App::OnTick()
{
    if (dirty) {
        dirty = false;
        settings.Save();
    }
    tray.SetTip(Paused() ? L"BlinkBuddy\n" + Lang::T(L"Reminders paused") : L"BlinkBuddy");
}

void App::ShowMenu()
{
    HMENU menu = CreatePopupMenu();
    HMENU pause = CreatePopupMenu();
    // (The settings window arrives in chunk 5: until then its item is shown, greyed.)
    AppendMenuW(menu, MF_STRING | MF_GRAYED, CmdSettings, Lang::T(L"Settings").c_str());
    SetMenuDefaultItem(menu, CmdSettings, FALSE);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
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
    case CmdPause30: Pause(30); break;
    case CmdPause60: Pause(60); break;
    case CmdPauseUntilResumed: Pause(-1); break;
    case CmdResume: pausedUntil = {}; break;
    case CmdExit: Exit(); break;
    }
}

void App::ShowSettings()
{
    // Chunk 5.
}

void App::Pause(int minutes)
{
    pausedUntil = minutes < 0 ? std::chrono::system_clock::time_point::max()
                              : std::chrono::system_clock::now() + std::chrono::minutes(minutes);
}

bool App::Paused() const { return std::chrono::system_clock::now() < pausedUntil; }

void App::Exit()
{
    KillTimer(window, TickTimer);
    if (dirty) settings.Save();
    tray.Remove();
    PostQuitMessage(0);
}

} // namespace bb
