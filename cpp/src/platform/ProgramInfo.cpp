#include "platform/ProgramInfo.h"
#include "platform/Text.h"
#include <shellapi.h>
#include <shlobj.h>
#include <cwctype>
#include <vector>

#pragma comment(lib, "version.lib")

namespace bb::ProgramInfo {

std::wstring Name(const std::wstring& app, const std::wstring& path)
{
    if (!path.empty()) {
        DWORD ignored = 0, size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
        std::vector<BYTE> info(size);
        struct Language { WORD language, codePage; }* languages = nullptr;
        UINT length = 0;
        if (size && GetFileVersionInfoW(path.c_str(), 0, size, info.data()) &&
            VerQueryValueW(info.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&languages), &length) && length >= 4) {
            wchar_t key[64];
            swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\FileDescription", languages[0].language, languages[0].codePage);
            wchar_t* description = nullptr;
            if (VerQueryValueW(info.data(), key, reinterpret_cast<void**>(&description), &length) && length > 1) {
                std::wstring d = Trim(description);
                if (!d.empty()) return d;
            }
        }
    }
    std::wstring n = app.size() > 4 && app.ends_with(L".exe") ? app.substr(0, app.size() - 4) : app;
    if (!n.empty()) n[0] = static_cast<wchar_t>(std::towupper(n[0]));
    return n;
}

HICON Icon(const std::wstring& path, int size)
{
    if (path.empty() || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return nullptr;
    HICON icon = nullptr;
    if (FAILED(SHDefExtractIconW(path.c_str(), 0, 0, &icon, nullptr, static_cast<UINT>(size)))) return nullptr;
    return icon;
}

} // namespace bb::ProgramInfo
