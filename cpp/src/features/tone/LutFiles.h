#pragma once
#include <windows.h>
#include <string>

namespace bb::LutFiles {

// The files the DesktopLUT engine needs: the colour table Softlid hands it, and DesktopLUT itself (a separate,
// free program, github.com/sup3rflyer/DesktopLUT), fetched only when you ask for it.

std::wstring CubePath();   // %AppData%\Softlid\DesktopLUT.cube
// The table for a strength (-1: one that changes nothing, for comparing): the smart curve on whole colours, 33 points.
bool WriteLevel(const std::wstring& path, int level);

constexpr wchar_t Page[] = L"https://github.com/sup3rflyer/DesktopLUT";
std::wstring Folder();     // where it is set up: %LocalAppData%\Softlid\DesktopLUT

// Fetches the latest release on a worker thread and unpacks DesktopLUT.exe and DwmHook.dll into Folder (nothing runs
// here). Progress lines arrive at `window` as `message` with wParam 0 and lParam a heap std::wstring* (the receiver
// deletes it); the end as wParam 1 (set up: Folder()\DesktopLUT.exe) or 2 (failed), with the message as lParam.
void Install(HWND window, UINT message);

} // namespace bb::LutFiles
