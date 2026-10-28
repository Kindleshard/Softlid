#pragma once
#include "core/Settings.h"
#include <windows.h>
#include <functional>
#include <string>

namespace bb {

// The DesktopLUT engine: DesktopLUT (a separate program) applies the colour table inside Windows' compositor, and
// BlinkBuddy watches over it from the outside. DesktopLUT runs as administrator, so the watching is done by a small
// elevated helper (this same exe with --lut-guard) that starts DesktopLUT itself and can always stop it. Stopping
// DesktopLUT removes the table, so every safeguard ends the same way: stop it, and BlinkBuddy's own filter takes over.
//   - Trial: after switching on, and after any Windows update, the screen must be confirmed within 15 s or it is undone.
//   - Watchdog: DesktopLUT exiting unexpectedly, the compositor restarting twice within a minute, or BlinkBuddy itself
//     closing or crashing stops it.
//   - Panic key: Ctrl+Alt+Shift+L stops it at any time, even when nothing on screen can be seen.
class LutGuard {
public:
    LutGuard() = default;
    ~LutGuard();
    LutGuard(const LutGuard&) = delete;
    LutGuard& operator=(const LutGuard&) = delete;


    void Ensure(const Settings& s, bool forceTrial);   // starts it if it isn't running (may fail at once)
    void Poll();                                       // once a second
    void Stop();
    void Reload();                                     // the table was rewritten (a new strength): restart DesktopLUT
    void ClearFailure();
    bool Running() const;
    bool Failed() const { return failed; }
    const std::wstring& Status() const { return status; }

    std::function<void()> changed;                     // it failed, stopped or was confirmed
    std::function<void(const std::wstring&)> confirmed;   // the Windows build the trial was confirmed on

private:
    void ReadStatus();
    void Fail(const std::wstring& why, bool notify = true);
    bool Command(const std::wstring& what);
    HANDLE helper = nullptr, stopEvent = nullptr, wakeEvent = nullptr;
    std::wstring lutName = L"DesktopLUT", status;
    FILETIME statusSeen{};
    int commandSeq = 0;
    bool failed = false, stopping = false;
};

// The elevated helper's whole life: BlinkBuddy.exe --lut-guard "<DesktopLUT.exe>" <trial 0/1> <BlinkBuddy pid> "<cube>".
int RunLutGuardHelper(const std::wstring& lutPath, bool trial, DWORD ownerPid, const std::wstring& cubePath);

} // namespace bb
