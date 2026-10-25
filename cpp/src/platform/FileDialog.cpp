#include "platform/FileDialog.h"
#include <commdlg.h>

#pragma comment(lib, "comdlg32.lib")

namespace bb::FileDialog {

bool PickProgram(HWND owner, const std::wstring& title, const std::wstring& filterName, std::wstring& path)
{
    // "Programs (*.exe)\0*.exe\0\0"
    std::wstring filter = filterName + L" (*.exe)";
    filter += L'\0';
    filter += L"*.exe";
    filter += L'\0';
    filter += L'\0';
    wchar_t file[MAX_PATH * 2] = {};
    OPENFILENAMEW ofn{sizeof ofn};
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.lpstrTitle = title.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return false;
    path = file;
    return true;
}

} // namespace bb::FileDialog
