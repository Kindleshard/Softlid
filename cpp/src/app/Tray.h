#pragma once
#include <windows.h>
#include <shellapi.h>
#include <string>

namespace bb {

// The icon in the notification area. Clicks arrive at the owner window as CallbackMessage, with the event
// (WM_CONTEXTMENU, NIN_SELECT, ...) in LOWORD(lParam).
class Tray {
public:
    static constexpr UINT CallbackMessage = WM_APP + 1;

    void Add(HWND owner, HICON icon, const std::wstring& tip);
    void SetTip(const std::wstring& tip);
    void Notify(const std::wstring& title, const std::wstring& text);   // a notification from the icon
    void Remove();
    void AddAgain();   // after Explorer restarts, the icon has to be added again

    // Where to open the menu: the pointer, as the click left it.
    static POINT MenuPoint();

private:
    NOTIFYICONDATAW data{};
    bool added = false;
};

} // namespace bb
