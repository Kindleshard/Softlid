#include "platform/ForegroundApp.h"
#include "platform/ProgramInfo.h"
#include "platform/Text.h"
#include <dwmapi.h>
#include <algorithm>
#include <unordered_map>

#pragma comment(lib, "dwmapi.lib")

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

// A program's file, by process id (programs running as administrator too); "" if it can't be read.
static std::wstring PathOf(DWORD pid)
{
    std::wstring path;
    if (HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t buffer[MAX_PATH * 2];
        DWORD size = MAX_PATH * 2;
        if (QueryFullProcessImageNameW(process, 0, buffer, &size)) path.assign(buffer, size);
        CloseHandle(process);
    }
    return path;
}

std::vector<ForegroundApp::Running> ForegroundApp::RunningApps()
{
    struct Found { std::vector<Running> list; std::vector<std::wstring> seen; } found;
    EnumWindows([](HWND w, LPARAM param) {
        auto* f = reinterpret_cast<Found*>(param);
        if (!IsWindowVisible(w) || GetWindowTextLengthW(w) == 0 || GetWindow(w, GW_OWNER)) return TRUE;
        LONG ex = GetWindowLongW(w, GWL_EXSTYLE);
        if (!(ex & WS_EX_APPWINDOW) && (ex & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT))) return TRUE;
        BOOL cloaked = FALSE;
        if (SUCCEEDED(DwmGetWindowAttribute(w, DWMWA_CLOAKED, &cloaked, sizeof cloaked)) && cloaked) return TRUE;
        if (Contains(PassingClasses, ClassOf(w))) return TRUE;
        DWORD pid = AppPid(w);
        std::wstring name = NameOf(pid);
        if (pid == GetCurrentProcessId() || name.empty() || Contains(PassingApps, name) || name == L"applicationframehost.exe") return TRUE;
        if (std::find(f->seen.begin(), f->seen.end(), name) != f->seen.end()) return TRUE;
        f->seen.push_back(name);
        wchar_t title[256] = {};
        GetWindowTextW(w, title, 256);
        f->list.push_back({name, title, PathOf(pid)});
        return TRUE;
    }, reinterpret_cast<LPARAM>(&found));
    std::sort(found.list.begin(), found.list.end(), [](const Running& a, const Running& b) {
        return CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE, ProgramInfo::Name(a.app, a.path).c_str(), -1,
                               ProgramInfo::Name(b.app, b.path).c_str(), -1, nullptr, nullptr, 0) == CSTR_LESS_THAN;
    });
    return found.list;
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
