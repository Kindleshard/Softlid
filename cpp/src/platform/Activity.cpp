#include "platform/Activity.h"
#include <shellapi.h>
#include <shellscalingapi.h>

#pragma comment(lib, "shcore.lib")

namespace bb {

int IdleSeconds()
{
    LASTINPUTINFO info{sizeof info};
    if (!GetLastInputInfo(&info)) return 0;
    return static_cast<int>((GetTickCount() - info.dwTime) / 1000);
}

bool FullscreenAppRunning()
{
    // Exclusive full-screen games and presentation mode, as Windows reports them.
    QUERY_USER_NOTIFICATION_STATE state{};
    if (SUCCEEDED(SHQueryUserNotificationState(&state)) && (state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE))
        return true;
    // Windows' "busy" state also counts any always-on-top window covering a screen, BlinkBuddy's own overlays
    // included, which would pause the reminders for good. So judge the window in use instead: someone else's window
    // covering its whole screen (a video, a borderless game, F11).
    HWND front = GetForegroundWindow();
    if (!front || front == GetShellWindow()) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(front, &pid);
    if (pid == GetCurrentProcessId()) return false;
    wchar_t cls[64] = {};
    GetClassNameW(front, cls, 64);
    if (lstrcmpW(cls, L"WorkerW") == 0 || lstrcmpW(cls, L"Progman") == 0) return false;   // the desktop
    RECT r{};
    if (!GetWindowRect(front, &r)) return false;
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorFromWindow(front, MONITOR_DEFAULTTONEAREST), &mi);
    return r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top && r.right >= mi.rcMonitor.right && r.bottom >= mi.rcMonitor.bottom;
}

static HMONITOR MonitorAtCursor()
{
    POINT p{};
    GetCursorPos(&p);
    return MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST);
}

RECT WorkAreaAtCursor()
{
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorAtCursor(), &mi);
    return mi.rcWork;
}

float ScaleAtCursor()
{
    UINT x = 96, y = 96;
    GetDpiForMonitor(MonitorAtCursor(), MDT_EFFECTIVE_DPI, &x, &y);
    return x / 96.0f;
}

float ScaleOf(HWND window) { return GetDpiForWindow(window) / 96.0f; }

float ScaleAt(POINT p)
{
    UINT x = 96, y = 96;
    GetDpiForMonitor(MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &x, &y);
    return x / 96.0f;
}

} // namespace bb
