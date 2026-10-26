#include "platform/MemJournal.h"
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <cstdio>
#include <share.h>
#include <map>
#include <set>
#include <vector>

namespace bb::MemJournal {
namespace {

FILE* out = nullptr;
std::wstring notes;
std::map<HMODULE, std::wstring> modules;   // (names kept: an unloaded one can no longer be asked)
long long lastPrivate = -1;
int lastThreads = -1;

int Threads()
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return -1;
    int n = 0;
    THREADENTRY32 t{sizeof t};
    for (BOOL ok = Thread32First(snap, &t); ok; ok = Thread32Next(snap, &t))
        if (t.th32OwnerProcessID == GetCurrentProcessId()) n++;
    CloseHandle(snap);
    return n;
}

std::wstring Name(HMODULE m)
{
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleBaseNameW(GetCurrentProcess(), m, path, MAX_PATH);
    return n ? std::wstring(path, n) : L"?";
}

} // namespace

void Start(const std::wstring& file)
{
    out = _wfsopen(file.c_str(), L"a, ccs=UTF-8", _SH_DENYNO);   // (readable while it runs)
    if (!out) return;
    fwprintf(out, L"---- BlinkBuddy started (pid %lu)%ls", GetCurrentProcessId(), L"\n");
    Note(L"start");
    Check();
}

void Note(const wchar_t* what)
{
    if (!out) return;
    if (!notes.empty()) notes += L"; ";
    notes += what;
}

void Check()
{
    if (!out) return;
    PROCESS_MEMORY_COUNTERS_EX2 m{};
    m.cb = sizeof m;
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m), sizeof m);
    long long priv = static_cast<long long>(m.PrivateUsage);
    // (counting threads walks every thread on the PC: tens of milliseconds, so only every 10 seconds)
    static int calls = 0;
    int threads = calls++ % 10 == 0 || !notes.empty() ? Threads() : lastThreads;

    std::vector<HMODULE> now(1024);
    DWORD bytes = 0;
    EnumProcessModules(GetCurrentProcess(), now.data(), static_cast<DWORD>(now.size() * sizeof(HMODULE)), &bytes);
    now.resize(bytes / sizeof(HMODULE));
    std::set<HMODULE> current(now.begin(), now.end());
    std::wstring loaded, unloaded;
    bool first = modules.empty();
    for (HMODULE h : current)
        if (!modules.count(h)) {
            modules[h] = Name(h);
            if (!first) loaded += L" +" + modules[h];
        }
    for (auto it = modules.begin(); it != modules.end();)
        if (!current.count(it->first)) {
            unloaded += L" -" + it->second;
            it = modules.erase(it);
        } else {
            ++it;
        }

    bool changed = !notes.empty() || !loaded.empty() || !unloaded.empty() || threads != lastThreads ||
                   lastPrivate < 0 || std::llabs(priv - lastPrivate) >= 256 * 1024;
    if (!changed) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fwprintf(out, L"%02d:%02d:%02d  set aside %5.1f MB  own in RAM %5.1f MB  threads %2d  %ls%ls%ls%ls", t.wHour, t.wMinute,
             t.wSecond, priv / 1048576.0, m.PrivateWorkingSetSize / 1048576.0, threads, notes.c_str(), loaded.c_str(),
             unloaded.c_str(), L"\n");
    fflush(out);
    notes.clear();
    lastPrivate = priv;
    lastThreads = threads;
}

} // namespace bb::MemJournal
