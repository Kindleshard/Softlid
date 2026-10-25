// BlinkBuddy: eye care that lives in the tray - blink and break reminders, a screen filter, and more.
// The entry point: one copy at a time, then the app.
#include "app/App.h"
#include <windows.h>
#include <objbase.h>
#include <cwchar>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int)
{
    // One copy at a time: a second start asks the running one to show its settings and leaves. (The same names as
    // the C# version, so the two never run side by side.)
    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\BlinkBuddy.Instance");
    bool first = GetLastError() != ERROR_ALREADY_EXISTS;
    HANDLE showRequest = CreateEventW(nullptr, FALSE, FALSE, L"Local\\BlinkBuddy.Show");
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
        // "--tray": start quietly (as at sign-in); otherwise the settings open.
        bool openSettings = std::wcsstr(commandLine, L"--tray") == nullptr;
        bb::App app(instance, showRequest, openSettings);
        code = app.Run();
    }
    CoUninitialize();
    CloseHandle(showRequest);
    ReleaseMutex(single);
    CloseHandle(single);
    return code;
}
