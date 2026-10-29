#include "features/tone/LutGuard.h"
#include "features/tone/LutFiles.h"
#include "core/Lang.h"
#include "platform/Files.h"
#include "platform/ProgramInfo.h"
#include "platform/Text.h"
#include "platform/Window.h"
#include "ui/DirectWindow.h"
#include "ui/Theme.h"
#include "ui/controls/Basic.h"
#include <dwmapi.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <algorithm>
#include <vector>

#pragma comment(lib, "dwmapi.lib")

namespace bb {
namespace {

constexpr wchar_t StopEventName[] = L"Local\\Softlid.LutStop";
constexpr wchar_t WakeEventName[] = L"Local\\Softlid.LutWake";

std::wstring StatusFile() { return RoamingAppData() + L"\\Softlid\\lut-guard.status"; }
std::wstring CommandFile() { return RoamingAppData() + L"\\Softlid\\lut-guard.command"; }

// The processes running a program, by its file name without ".exe" (any letter case).
std::vector<DWORD> ProcessesNamed(const std::wstring& name)
{
    std::vector<DWORD> found;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return found;
    PROCESSENTRY32W e{sizeof e};
    for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e))
        if (EqualsNoCase(e.szExeFile, name + L".exe")) found.push_back(e.th32ProcessID);
    CloseHandle(snap);
    return found;
}

// ---- Making Windows recompose the whole screen ----

// Windows recomposes only the parts of the screen that change, so a table switched on, changed or off would show only
// where something moves and the rest would keep the old picture. Covering the screen for a moment with an all-but-
// invisible window makes Windows recompose all of it.
class Refresher : public Window {
public:
    Refresher()
    {
        RECT all{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), 0, 0};
        all.right = all.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
        all.bottom = all.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
        Create(L"Softlid.Refresh", WS_POPUP, PopupExStyle | WS_EX_LAYERED | WS_EX_TRANSPARENT, all, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        SetLayeredWindowAttributes(hwnd, 0, 3, LWA_ALPHA);   // darkens by 1% for two frames: invisible, but everything under it is redrawn
    }

    void Now()
    {
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        DwmFlush();   // wait for the frames that include it, then for the ones without it
        DwmFlush();
        ShowWindow(hwnd, SW_HIDE);
        DwmFlush();
    }

    // Refresh at each of these times (ms from now); a new call replaces the refreshes still waiting.
    void After(std::vector<int> times)
    {
        pending = std::move(times);
        step = 0;
        if (!pending.empty()) SetTimer(hwnd, 1, pending[0], nullptr);
    }

private:
    LRESULT OnMessage(UINT message, WPARAM w, LPARAM l) override
    {
        if (message == WM_TIMER) {
            KillTimer(hwnd, 1);
            Now();
            if (++step < pending.size()) SetTimer(hwnd, 1, pending[step] - pending[step - 1], nullptr);
            return 0;
        }
        return Window::OnMessage(message, w, l);
    }
    std::vector<int> pending;
    size_t step = 0;
};

// A job that Windows closes when this process ends in any way, ending everything in it.
HANDLE KillOnExitJob()
{
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) return nullptr;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits);
    return job;
}

// ---- The helper (elevated) ----

// "Keep these display settings?" for DesktopLUT: it undoes itself unless someone who can see the screen says yes.
class TrialDialog : public ui::DirectWindow {
public:
    std::function<void(bool kept, bool timedOut)> answered;

    TrialDialog()
    {
        using namespace ui;
        Root().Add<Label>(L"Does your screen look right?", Fonts::Heading()).SetBounds(20, 16, 380, 28);
        countdown = &Root().Add<Label>(L"", Fonts::Small(), Theme::Dim);
        countdown->wrap = countdown->top = true;
        countdown->SetBounds(20, 48, 380, 44);
        auto& keep = Root().Add<Button>(L"Keep");
        keep.SetBounds(196, 112, 96, 36);
        keep.onClick = [this] { Answer(true, false); };
        auto& undo = Root().Add<Button>(L"Undo");
        undo.SetBounds(304, 112, 96, 36);
        undo.onClick = [this] { Answer(false, false); };
        Tick();
    }

    void Start()
    {
        Options opts;
        opts.title = L"Softlid: keep DesktopLUT?";
        opts.dialog = true;
        Open(420, 170, nullptr, opts);
        SetWindowPos(Handle(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        Show(true);
        SetTimer(Handle(), 1, 1000, nullptr);
    }

protected:
    bool Shortcut(UINT key) override
    {
        if (key == VK_RETURN) Answer(true, false);
        else if (key == VK_ESCAPE) Answer(false, false);
        else return false;
        return true;
    }

    LRESULT OnMessage(UINT message, WPARAM w, LPARAM l) override
    {
        if (message == WM_TIMER && w == 1) {
            Tick();
            return 0;
        }
        if (message == WM_CLOSE && !done) Answer(false, false);   // closing it is "no"
        return DirectWindow::OnMessage(message, w, l);
    }

private:
    void Tick()
    {
        if (left <= 0) {
            Answer(false, true);
            return;
        }
        countdown->SetPlainText(Lang::F(L"DesktopLUT is now applying the colour table. If you do nothing, it is undone in {0} s.\nPanic key at any time: Ctrl+Alt+Shift+L.",
                                        {std::to_wstring(left--)}));
    }

    void Answer(bool kept, bool timedOut)
    {
        if (done) return;
        done = true;
        if (Handle()) {
            KillTimer(Handle(), 1);
            ShowWindow(Handle(), SW_HIDE);
        }
        if (answered) answered(kept, timedOut);
    }

    ui::Label* countdown = nullptr;
    int left = 15;
    bool done = false;
};

class Helper : public Window {
public:
    Helper(const std::wstring& path, bool trial, DWORD ownerPid, const std::wstring& cube)
        : lutPath(path), cubePath(cube), job(KillOnExitJob())
    {
        lutName = path.substr(path.find_last_of(L'\\') + 1);
        if (lutName.size() > 4) lutName.resize(lutName.size() - 4);
        owner = OpenProcess(SYNCHRONIZE, FALSE, ownerPid);
        stopEvent = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, StopEventName);
        if (!stopEvent) stopEvent = CreateEventW(nullptr, TRUE, FALSE, StopEventName);
        wakeEvent = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, WakeEventName);
        if (!wakeEvent) wakeEvent = CreateEventW(nullptr, FALSE, FALSE, WakeEventName);
        Create(L"Softlid.LutGuard", WS_POPUP, WS_EX_TOOLWINDOW, {0, 0, 0, 0});
        RegisterHotKey(hwnd, 1, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'L');
        dwmPid = DwmPid();
        if (!Launch()) return;
        Write(L"running", trial ? L"Checking: confirm within 15 seconds that the screen looks right." : L"DesktopLUT is running, watched by Softlid.");
        SetTimer(hwnd, 1, 1000, nullptr);
        // Commands from Softlid ("run": restart DesktopLUT, the only way to make it load a changed table), handled
        // on this thread the moment they arrive, so the watchdog can't mistake the moment in between for a crash.
        RegisterWaitForSingleObject(&wakeWait, wakeEvent, [](void* self, BOOLEAN) {
            PostMessageW(static_cast<Helper*>(self)->hwnd, WM_APP + 1, 0, 0);
        }, this, INFINITE, WT_EXECUTEDEFAULT);
        if (trial) {
            dialog = std::make_unique<TrialDialog>();
            dialog->answered = [this](bool kept, bool timedOut) {
                if (done) return;
                if (kept) Write(L"confirmed", L"DesktopLUT is running, watched by Softlid (confirmed on this Windows build).", ProgramInfo::WindowsBuild());
                else Finish(L"failed", timedOut ? L"No confirmation within 15 seconds, so DesktopLUT was stopped and Softlid's own filter took over."
                                                : L"Undone. DesktopLUT was stopped and Softlid's own filter took over.");
            };
            dialog->Start();
        }
    }

    ~Helper() override
    {
        if (wakeWait) UnregisterWaitEx(wakeWait, INVALID_HANDLE_VALUE);
        for (HANDLE h : {owner, stopEvent, wakeEvent}) if (h) CloseHandle(h);
        dialog.reset();
        Destroy();
        if (job) CloseHandle(job);   // (takes DesktopLUT with it, if it is somehow still running)
    }

private:
    // (Re)starts DesktopLUT pointed at Softlid's table. A copy already running is closed first, so it can't save its
    // old table over ours on the way out.
    bool Launch()
    {
        CloseLut();
        std::wstring folder = lutPath.substr(0, lutPath.find_last_of(L'\\')), ini = folder + L"\\DesktopLUT.ini";
        WritePrivateProfileStringW(L"General", L"StartMinimized", L"true", ini.c_str());   // no window popping up on restarts
        for (int i = 0; i < GetSystemMetrics(SM_CMONITORS); i++)
            WritePrivateProfileStringW((L"Monitor" + std::to_wstring(i)).c_str(), L"LUT_SDR", cubePath.c_str(), ini.c_str());
        STARTUPINFOW si{sizeof si};
        PROCESS_INFORMATION pi{};
        std::wstring command = L"\"" + lutPath + L"\"";
        if (!CreateProcessW(lutPath.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, folder.c_str(), &si, &pi)) {
            Finish(L"failed", L"DesktopLUT couldn't be started (error " + std::to_wstring(GetLastError()) + L").");
            return false;
        }
        if (job) AssignProcessToJobObject(job, pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        // DesktopLUT doesn't say when its new table is in place, so refresh every 150 ms for 3 s, then once more.
        std::vector<int> times;
        for (int i = 1; i <= 20; i++) times.push_back(i * 150);
        times.push_back(5000);
        refresher.After(times);
        return true;
    }

    void CloseLut()
    {
        for (DWORD pid : ProcessesNamed(lutName)) {
            HANDLE p = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pid);
            if (!p) continue;
            // Its windows asked to close first; minimised to the tray it may have none, so then it is ended.
            EnumWindows([](HWND w, LPARAM id) {
                DWORD owner = 0;
                GetWindowThreadProcessId(w, &owner);
                if (owner == static_cast<DWORD>(id) && IsWindowVisible(w)) PostMessageW(w, WM_CLOSE, 0, 0);
                return TRUE;
            }, pid);
            if (WaitForSingleObject(p, 2000) != WAIT_OBJECT_0) TerminateProcess(p, 0);
            WaitForSingleObject(p, 3000);
            CloseHandle(p);
        }
    }

    static DWORD DwmPid()
    {
        DWORD session = 0;
        ProcessIdToSessionId(GetCurrentProcessId(), &session);
        for (DWORD pid : ProcessesNamed(L"dwm")) {
            DWORD s = 0;
            if (ProcessIdToSessionId(pid, &s) && s == session) return pid;
        }
        return 0;
    }

    void OnCommand()
    {
        if (done) return;
        std::wstring text = Trim(ReadAllText(CommandFile()));
        size_t space = text.find(L' ');
        if (space == std::wstring::npos) return;
        int seq = _wtoi(text.c_str() + space + 1);
        if (seq == lastCommand) return;
        lastCommand = seq;
        if (text.substr(0, space) == L"run" && Launch()) Write(L"running", L"DesktopLUT is running, watched by Softlid.");
    }

    void Check()
    {
        if (done) return;
        if (WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0) return Finish(L"stopped", L"DesktopLUT was stopped.");
        if (!owner || WaitForSingleObject(owner, 0) == WAIT_OBJECT_0) return Finish(L"stopped", L"Softlid closed, so DesktopLUT was stopped.");
        if (ProcessesNamed(lutName).empty()) return Finish(L"failed", L"DesktopLUT closed or crashed, so Softlid's own filter took over.");
        // A compositor restart can happen for harmless reasons (a driver update); two within a minute means trouble.
        DWORD pid = DwmPid();
        if (pid && pid != dwmPid) {
            dwmPid = pid;
            ULONGLONG now = GetTickCount64();
            std::erase_if(dwmRestarts, [now](ULONGLONG t) { return now - t >= 60000; });
            dwmRestarts.push_back(now);
            if (dwmRestarts.size() >= 2)
                Finish(L"failed", L"Windows' compositor restarted twice within a minute, so DesktopLUT was stopped as a precaution.");
        }
    }

    // Every way out ends here: stop DesktopLUT (which removes its table), report, and close.
    void Finish(const std::wstring& state, const std::wstring& message)
    {
        if (done) return;
        done = true;
        KillTimer(hwnd, 1);
        CloseLut();
        // DesktopLUT's hook lets go a moment after it ends; refresh after that so no part of the screen keeps its picture.
        Sleep(400);
        refresher.Now();
        Sleep(800);
        refresher.Now();
        Write(state, message);
        UnregisterHotKey(hwnd, 1);
        PostQuitMessage(0);
    }

    static void Write(const std::wstring& state, const std::wstring& message, const std::wstring& extra = L"")
    {
        WriteAllText(StatusFile(), state + L"|" + message + (extra.empty() ? L"" : L"|" + extra));
    }

    LRESULT OnMessage(UINT message, WPARAM w, LPARAM l) override
    {
        switch (message) {
        case WM_TIMER: Check(); return 0;
        case WM_APP + 1: OnCommand(); return 0;
        case WM_HOTKEY: Finish(L"failed", L"Stopped with Ctrl+Alt+Shift+L. Softlid's own filter took over."); return 0;
        }
        return Window::OnMessage(message, w, l);
    }

    std::wstring lutPath, lutName, cubePath;
    HANDLE job, owner = nullptr, stopEvent = nullptr, wakeEvent = nullptr, wakeWait = nullptr;
    DWORD dwmPid = 0;
    std::vector<ULONGLONG> dwmRestarts;
    int lastCommand = 0;
    bool done = false;
    Refresher refresher;
    std::unique_ptr<TrialDialog> dialog;
};

} // namespace

int RunLutGuardHelper(const std::wstring& lutPath, bool trial, DWORD ownerPid, const std::wstring& cubePath)
{
    Helper helper(lutPath, trial, ownerPid, cubePath);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

// ---- The app's side ----

LutGuard::~LutGuard()
{
    Stop();
    if (stopEvent) CloseHandle(stopEvent);
    if (wakeEvent) CloseHandle(wakeEvent);
}

bool LutGuard::Running() const { return helper && WaitForSingleObject(helper, 0) == WAIT_TIMEOUT; }

void LutGuard::Ensure(const Settings& s, bool forceTrial)
{
    if (Running() || failed) return;
    if (GetFileAttributesW(s.LutPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Fail(L"DesktopLUT wasn't found at " + s.LutPath + L".", false);
        return;
    }
    lutName = s.LutPath.substr(s.LutPath.find_last_of(L'\\') + 1);
    if (lutName.size() > 4) lutName.resize(lutName.size() - 4);
    bool trial = forceTrial || s.LutConfirmedBuild != ProgramInfo::WindowsBuild();
    if (!stopEvent) stopEvent = CreateEventW(nullptr, TRUE, FALSE, StopEventName);
    ResetEvent(stopEvent);
    if (!wakeEvent) wakeEvent = CreateEventW(nullptr, FALSE, FALSE, WakeEventName);
    ResetEvent(wakeEvent);
    DeleteFileW(StatusFile().c_str());
    DeleteFileW(CommandFile().c_str());
    statusSeen = {};
    std::wstring args = L"--lut-guard \"" + s.LutPath + L"\" " + (trial ? L"1 " : L"0 ") + std::to_wstring(GetCurrentProcessId()) + L" \"" +
                        LutFiles::CubePath() + L"\"";
    std::wstring exe = ExePath();
    SHELLEXECUTEINFOW sei{sizeof sei};
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = exe.c_str();
    sei.lpParameters = args.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei)) {
        DWORD error = GetLastError();
        Fail(error == ERROR_CANCELLED ? L"Administrator permission was declined, so DesktopLUT wasn't started."
                                      : L"Couldn't start the guard (error " + std::to_wstring(error) + L").",
             false);
        return;
    }
    helper = sei.hProcess;
    stopping = false;
    status = trial ? L"Starting DesktopLUT for a 15-second check..." : L"Starting DesktopLUT...";
}

void LutGuard::Poll()
{
    if (!helper) return;
    ReadStatus();
    if (!helper || WaitForSingleObject(helper, 0) == WAIT_TIMEOUT) return;   // a reported failure may already have stopped it
    ReadStatus();
    if (!helper) return;
    CloseHandle(helper);
    helper = nullptr;
    if (!stopping) {
        // The guard was killed before it could stop DesktopLUT (its job normally takes DesktopLUT with it). Try from here
        // too; this works unless DesktopLUT refuses a non-administrator.
        for (DWORD pid : ProcessesNamed(lutName))
            if (HANDLE p = OpenProcess(PROCESS_TERMINATE, FALSE, pid)) {
                TerminateProcess(p, 0);
                CloseHandle(p);
            }
        if (!failed) Fail(L"The guard closed unexpectedly, so DesktopLUT was stopped and Softlid's own filter took over.");
    }
    stopping = false;
    if (changed) changed();
}

void LutGuard::ReadStatus()
{
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW(StatusFile().c_str(), GetFileExInfoStandard, &info) || CompareFileTime(&info.ftLastWriteTime, &statusSeen) <= 0) return;
    statusSeen = info.ftLastWriteTime;
    std::wstring text = Trim(ReadAllText(StatusFile()));
    size_t a = text.find(L'|');
    if (a == std::wstring::npos) return;
    size_t b = text.find(L'|', a + 1);
    std::wstring state = text.substr(0, a), message = text.substr(a + 1, b == std::wstring::npos ? std::wstring::npos : b - a - 1);
    status = message;
    if (state == L"confirmed" && b != std::wstring::npos && confirmed) confirmed(text.substr(b + 1));
    if (state == L"failed" && !stopping) Fail(message);
}

// notify is false inside Ensure: the caller is already applying the screen and checks Failed afterwards.
void LutGuard::Fail(const std::wstring& why, bool notify)
{
    failed = true;
    status = why;
    if (notify && changed) changed();
}

void LutGuard::ClearFailure()
{
    failed = false;
    status.clear();
}

void LutGuard::Stop()
{
    if (!helper) return;
    stopping = true;
    if (stopEvent) SetEvent(stopEvent);
    if (WaitForSingleObject(helper, 6000) == WAIT_OBJECT_0) {
        CloseHandle(helper);
        helper = nullptr;
    }
    if (!failed) status.clear();   // the reason it failed stays on show
}

void LutGuard::Reload()
{
    if (Command(L"run")) status = L"Applying the new strength...";
}

// "run <n>" in the command file, then a wake-up. The guard acts on the newest command only, so a quick run of
// strength changes restarts DesktopLUT for the last one rather than for each.
bool LutGuard::Command(const std::wstring& what)
{
    if (!Running() || !wakeEvent) return false;
    if (!WriteAllBytesSafely(CommandFile(), ToUtf8(what + L" " + std::to_wstring(++commandSeq)))) return false;
    SetEvent(wakeEvent);
    return true;
}

} // namespace bb
