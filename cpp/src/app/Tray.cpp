#include "app/Tray.h"

namespace bb {

void Tray::Add(HWND owner, HICON icon, const std::wstring& tip)
{
    data.cbSize = sizeof data;
    data.hWnd = owner;
    data.uID = 1;
    data.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;
    data.uCallbackMessage = CallbackMessage;
    data.hIcon = icon;
    wcsncpy_s(data.szTip, tip.c_str(), _TRUNCATE);
    added = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
    data.uVersion = NOTIFYICON_VERSION_4;   // clicks as WM_CONTEXTMENU / NIN_SELECT, with the menu point
    Shell_NotifyIconW(NIM_SETVERSION, &data);
}

void Tray::SetTip(const std::wstring& tip)
{
    if (tip == data.szTip) return;
    wcsncpy_s(data.szTip, tip.c_str(), _TRUNCATE);   // (Windows keeps 127 characters)
    if (added) Shell_NotifyIconW(NIM_MODIFY, &data);
}

void Tray::Notify(const std::wstring& title, const std::wstring& text)
{
    if (!added) return;
    NOTIFYICONDATAW n = data;
    n.uFlags = NIF_INFO;
    n.dwInfoFlags = NIIF_INFO;
    wcsncpy_s(n.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(n.szInfo, text.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

void Tray::Remove()
{
    if (added) Shell_NotifyIconW(NIM_DELETE, &data);
    added = false;
}

void Tray::AddAgain()
{
    added = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
    Shell_NotifyIconW(NIM_SETVERSION, &data);
}

POINT Tray::MenuPoint()
{
    POINT p{};
    GetCursorPos(&p);
    return p;
}

} // namespace bb
