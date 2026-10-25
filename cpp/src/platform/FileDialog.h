#pragma once
#include <windows.h>
#include <string>

namespace bb::FileDialog {

// Windows' Open dialog for a program (*.exe); false if cancelled. title and filterName are already translated.
bool PickProgram(HWND owner, const std::wstring& title, const std::wstring& filterName, std::wstring& path);

} // namespace bb::FileDialog
