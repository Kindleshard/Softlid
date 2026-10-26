// BlinkBuddy: eye care that lives in the tray - blink and break reminders, a screen filter, and more.
// The entry point: one copy at a time, then the app.
#include "app/App.h"
#include "core/Lang.h"
#include "features/breaks/GuidedBreak.h"
#include "features/breaks/StretchBreak.h"
#include "features/tone/LutGuard.h"
#include "features/update/Updater.h"
#include "tools/ControlGallery.h"
#include "ui/Theme.h"
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <cwchar>

// Diagnostics: "--preview-break" shows a look-away break card with your settings, "--preview-exercises" the guided
// eye exercises and "--preview-stretch" the stretch coach, then exits (no tray, and it runs beside a BlinkBuddy that is
// already running).
enum class Preview { Card, Exercises, Stretch };

static int PreviewBreak(Preview kind)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bb::Settings settings = bb::Settings::Load();
    bb::Lang::Use(settings.Language);
    bb::Theme::UseAccent(settings);
    // With no owner, "done" arrives on this thread's queue.
    std::unique_ptr<bb::Window> shown;
    if (kind == Preview::Exercises) shown = std::make_unique<bb::GuidedBreak>(nullptr, settings, 0.0f);
    else if (kind == Preview::Stretch) shown = std::make_unique<bb::StretchBreak>(nullptr, settings);
    else shown = std::make_unique<bb::BreakCard>(nullptr, false, settings);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0 && msg.message != bb::BreakCard::DoneMessage) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return 0;
}

// Diagnostics: "--preview-blink" plays the blink reminder once with your settings (the eye, or the eyelids), then exits.
static int PreviewBlink()
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bb::Settings settings = bb::Settings::Load();
    bb::Theme::UseAccent(settings);
    bb::EyePopup eye;
    std::unique_ptr<bb::Eyelids> lids;
    if (settings.BlinkStyle == 1) {
        lids = std::make_unique<bb::Eyelids>();
        lids->Play(settings);
    } else {
        eye.Play(settings, false);
    }
    SetTimer(nullptr, 0, 2200, nullptr);   // a little longer than the blink
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0 && !(msg.message == WM_TIMER && msg.hwnd == nullptr)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return 0;
}

// Diagnostics: "--ui-gallery" shows every control of the interface toolkit (see tools/ControlGallery).
static int UiGallery(bool offscreen)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bb::Settings settings = bb::Settings::Load();
    bb::Lang::Use(settings.Language);
    bb::Theme::UseAccent(settings);
    int code = bb::RunControlGallery(offscreen);
    CoUninitialize();
    return code;
}

// The DesktopLUT guard: BlinkBuddy starts this copy as administrator (see features/tone/LutGuard), with
// --lut-guard "<DesktopLUT.exe>" <trial 0/1> <BlinkBuddy pid> "<colour table>".
static int LutGuard()
{
    int count = 0;
    LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args || count < 6) {
        if (args) LocalFree(args);
        return 1;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bb::Settings settings = bb::Settings::Load();
    bb::Lang::Use(settings.Language);
    bb::Theme::UseAccent(settings);
    int code = bb::RunLutGuardHelper(args[2], args[3][0] == L'1', static_cast<DWORD>(_wtoi(args[4])), args[5]);
    LocalFree(args);
    CoUninitialize();
    return code;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int)
{
    if (std::wcsstr(commandLine, L"--lut-guard")) return LutGuard();
    if (std::wcsstr(commandLine, L"--preview-break")) return PreviewBreak(Preview::Card);
    if (std::wcsstr(commandLine, L"--preview-exercises")) return PreviewBreak(Preview::Exercises);
    if (std::wcsstr(commandLine, L"--preview-stretch")) return PreviewBreak(Preview::Stretch);
    if (std::wcsstr(commandLine, L"--preview-blink")) return PreviewBlink();
    if (std::wcsstr(commandLine, L"--ui-gallery")) return UiGallery(std::wcsstr(commandLine, L"--offscreen") != nullptr);

    // Just updated: the old copy is still quitting (it holds the single-copy lock until then).
    bb::Updater::AfterUpdate(commandLine);

    // Diagnostics: "--offscreen" runs a copy beside the real one, for screenshots (tools/ui-check.ps1): no single-copy
    // lock, no tray icon, no shortcuts, and with "--settings <file>" a settings file of its own. Its Resource use section
    // shows dashes, so screenshots stay the same; "--measure" makes it measure.
    bool offscreen = std::wcsstr(commandLine, L"--offscreen") != nullptr;
    if (int count = 0; LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count)) {
        for (int i = 1; i + 1 < count; i++)
            if (std::wcscmp(args[i], L"--settings") == 0) bb::Settings::UseFile(args[i + 1]);
        LocalFree(args);
    }

    // One copy at a time: a second start asks the running one to show its settings and leaves. (The same names as
    // the C# version, so the two never run side by side.)
    HANDLE single = offscreen ? nullptr : CreateMutexW(nullptr, TRUE, L"Local\\BlinkBuddy.Instance");
    bool first = offscreen || GetLastError() != ERROR_ALREADY_EXISTS;
    HANDLE showRequest = CreateEventW(nullptr, FALSE, FALSE, offscreen ? nullptr : L"Local\\BlinkBuddy.Show");
    if (!first) {
        AllowSetForegroundWindow(ASFW_ANY);
        SetEvent(showRequest);
        CloseHandle(showRequest);
        CloseHandle(single);
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int code;
    {
        // "--tray": start quietly (as at sign-in); otherwise the settings open. ("--offscreen": diagnostics, see App.)
        bool openSettings = std::wcsstr(commandLine, L"--tray") == nullptr;
        bb::App app(instance, showRequest, openSettings, offscreen);
        code = app.Run();
    }
    CoUninitialize();
    CloseHandle(showRequest);
    if (single) {
        ReleaseMutex(single);
        CloseHandle(single);
    }
    return code;
}
