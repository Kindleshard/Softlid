#include "app/OldName.h"
#include "platform/Files.h"
#include <windows.h>
#include <shlobj.h>

namespace bb::OldName {

namespace {

constexpr wchar_t RunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

bool Exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

std::wstring Known(REFKNOWNFOLDERID id)
{
    PWSTR folder = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &folder))) out = folder;
    CoTaskMemFree(folder);
    return out;
}

// Its single-copy lock is free once it has quit (it saves its settings and puts the screen back first).
void QuitOldCopy()
{
    HANDLE lock = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\BlinkBuddy.Instance");
    if (!lock) return;
    if (HWND old = FindWindowW(L"BlinkBuddy.App", nullptr)) PostMessageW(old, WM_APP + 7, 0, 0);   // (its QuitMessage)
    DWORD waited = WaitForSingleObject(lock, 10000);
    if (waited == WAIT_OBJECT_0 || waited == WAIT_ABANDONED) ReleaseMutex(lock);
    CloseHandle(lock);
}

// Pointed at this copy, as switching "Start with Windows" on would.
void MoveStartEntry()
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RunKey, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    if (RegQueryValueExW(key, L"BlinkBuddy", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        RegDeleteValueW(key, L"BlinkBuddy");
        std::wstring command = L"\"" + ExePath() + L"\" --tray";
        RegSetValueExW(key, L"Softlid", 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                       static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(key);
}

} // namespace

bool MoveFolder(const std::wstring& from, const std::wstring& to)
{
    if (!Exists(from) || Exists(to)) return false;
    if (MoveFileExW(from.c_str(), to.c_str(), 0)) return true;
    std::wstring settings = from + L"\\settings.ini";
    if (Exists(settings) && CreateDirectoryW(to.c_str(), nullptr))
        CopyFileW(settings.c_str(), (to + L"\\settings.ini").c_str(), TRUE);
    return true;
}

void MoveOver()
{
    QuitOldCopy();
    std::wstring roaming = RoamingAppData(), local = Known(FOLDERID_LocalAppData);
    MoveFolder(roaming + L"\\BlinkBuddy", roaming + L"\\Softlid");
    MoveFolder(local + L"\\BlinkBuddy", local + L"\\Softlid");
    MoveStartEntry();
}

} // namespace bb::OldName
