#include "platform/ForegroundApp.h"
#include "platform/Text.h"
#include <unordered_map>

namespace bb {
namespace {

// Windows that only pass through the front, by class and by program.
constexpr const wchar_t* PassingClasses[] = {
    L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd", L"Progman", L"WorkerW", L"Windows.UI.Core.CoreWindow",
    L"MultitaskingViewFrame", L"XamlExplorerHostIslandWindow", L"TaskListThumbnailWnd", L"NotifyIconOverflowWindow",
    L"TopLevelWindowForOverflowXamlIsland", L"ForegroundStaging", L"#32768", L"tooltips_class32",
};
constexpr const wchar_t* PassingApps[] = {
    L"shellexperiencehost.exe", L"startmenuexperiencehost.exe", L"searchhost.exe", L"searchapp.exe", L"lockapp.exe",
    L"shellhost.exe", L"textinputhost.exe",
};

template <size_t N>
bool Contains(const wchar_t* const (&list)[N], const std::wstring& s)
{
    for (const wchar_t* item : list) if (s == item) return true;
    return false;
}

std::wstring ClassOf(HWND window)
{
    wchar_t name[128] = {};
    GetClassNameW(window, name, 128);
    return name;
}

// A process's program as "name.exe" in lower case ("" if it can't be read; programs running as administrator can).
std::wstring NameOf(DWORD pid)
{
    static std::unordered_map<DWORD, std::wstring> names;
    if (auto found = names.find(pid); found != names.end()) return found->second;
    std::wstring name;
    if (HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t path[MAX_PATH * 2];
        DWORD size = MAX_PATH * 2;
        if (QueryFullProcessImageNameW(process, 0, path, &size)) {
            std::wstring full(path, size);
            name = Lower(full.substr(full.find_last_of(L'\\') + 1));
        }
        CloseHandle(process);
    }
    if (names.size() > 500) names.clear();   // process ids get reused; keep it small
    names[pid] = name;
    return name;
}

// The process a window belongs to; for a Store app (whose frame belongs to ApplicationFrameHost), the app inside it.
DWORD AppPid(HWND window)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (NameOf(pid) != L"applicationframehost.exe") return pid;
    struct Search { DWORD frame, inner; } search{pid, 0};
    EnumChildWindows(window, [](HWND child, LPARAM param) {
        auto* s = reinterpret_cast<Search*>(param);
        DWORD p = 0;
        GetWindowThreadProcessId(child, &p);
        if (p == s->frame) return TRUE;
        s->inner = p;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.inner ? search.inner : pid;
}

HWND frontTarget;
UINT frontMessage;

void CALLBACK OnForeground(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD)
{
    PostMessageW(frontTarget, frontMessage, 0, 0);
}

} // namespace

bool ForegroundApp::Get(std::wstring& app, HWND& window)
{
    window = GetForegroundWindow();
    app.clear();
    if (!window || Contains(PassingClasses, ClassOf(window))) return false;
    DWORD pid = AppPid(window);
    if (pid == 0 || pid == GetCurrentProcessId()) return false;
    app = NameOf(pid);
    return !Contains(PassingApps, app);
}

bool ForegroundApp::StillShown(HWND window)
{
    return window && IsWindow(window) && IsWindowVisible(window) && !IsIconic(window);
}

FrontWatch::FrontWatch(HWND window, UINT message)
{
    frontTarget = window;
    frontMessage = message;
    // Out of context, so it arrives through this thread's message loop (our own windows count too).
    hook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, OnForeground, 0, 0, WINEVENT_OUTOFCONTEXT);
}

FrontWatch::~FrontWatch()
{
    if (hook) UnhookWinEvent(hook);
}

} // namespace bb
