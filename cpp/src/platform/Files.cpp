#include "platform/Files.h"
#include "platform/Text.h"
#include <windows.h>
#include <shlobj.h>

namespace bb {

std::optional<std::string> ReadAllBytes(const std::wstring& path)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return std::nullopt;
    LARGE_INTEGER size{};
    std::optional<std::string> out;
    if (GetFileSizeEx(file, &size) && size.QuadPart < (1 << 26)) {   // (settings and the like: never this big)
        std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
        DWORD read = 0;
        if (ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size())
            out = std::move(bytes);
    }
    CloseHandle(file);
    return out;
}

bool WriteAllBytesSafely(const std::wstring& path, std::string_view bytes)
{
    size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) SHCreateDirectoryExW(nullptr, path.substr(0, slash).c_str(), nullptr);
    std::wstring temp = path + L".tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size();
    CloseHandle(file);
    ok = ok && MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) DeleteFileW(temp.c_str());
    return ok;
}

std::wstring RoamingAppData()
{
    PWSTR folder = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &folder))) out = folder;
    CoTaskMemFree(folder);
    return out;
}

std::wstring ReadAllText(const std::wstring& path) { return FromUtf8(ReadAllBytes(path).value_or("")); }

bool WriteAllText(const std::wstring& path, std::wstring_view text) { return WriteAllBytesSafely(path, ToUtf8(text)); }

std::wstring ExePath()
{
    wchar_t path[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    return std::wstring(path, n);
}

std::wstring ExeFolder()
{
    std::wstring path = ExePath();
    return path.substr(0, path.find_last_of(L'\\'));
}

std::wstring TempFolder()
{
    // (Asked once for the length, as the path may be longer than MAX_PATH; a failure gives "".)
    DWORD n = GetTempPathW(0, nullptr);
    std::wstring path(n, L'\0');
    n = n ? GetTempPathW(n, path.data()) : 0;
    path.resize(n < path.size() ? n : 0);
    return path;
}

} // namespace bb
