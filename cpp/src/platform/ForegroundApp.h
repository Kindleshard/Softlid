#pragma once
#include <windows.h>
#include <string>

namespace bb {

// The app in front, for the per-app screen modes.
namespace ForegroundApp {

// The app in front as "name.exe" in lower case ("" when unknown), and its window. False when the window in front only
// passes through - the taskbar, Start, search, Alt+Tab, the desktop, menus, BlinkBuddy itself: then whatever applied
// before should go on applying. A Store app counts as itself, not as the frame Windows wraps it in.
bool Get(std::wstring& app, HWND& window);

// Whether a window is still there to be seen (not closed, hidden or minimised).
bool StillShown(HWND window);

} // namespace ForegroundApp

// Tells when another window comes to the front, at once (rather than on the next second's look), by posting
// `message` to `window`.
class FrontWatch {
public:
    FrontWatch(HWND window, UINT message);
    ~FrontWatch();
    FrontWatch(const FrontWatch&) = delete;
    FrontWatch& operator=(const FrontWatch&) = delete;

private:
    HWINEVENTHOOK hook = nullptr;
};

} // namespace bb
