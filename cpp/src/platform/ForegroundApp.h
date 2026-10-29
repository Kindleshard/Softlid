#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace bb {

// The app in front, for the per-app screen modes.
namespace ForegroundApp {

// The app in front as "name.exe" in lower case ("" when unknown), and its window. False when the window in front only
// passes through - the taskbar, Start, search, Alt+Tab, the desktop, menus, Softlid itself: then whatever applied
// before should go on applying. A Store app counts as itself, not as the frame Windows wraps it in.
bool Get(std::wstring& app, HWND& window);

// Whether a window is still there to be seen (not closed, hidden or minimised).
bool StillShown(HWND window);

// Apps with a window you can see on the taskbar right now (to pick from when adding a rule), with a window title
// each, sorted by name. Not: hidden or cloaked windows, tool and overlay windows (e.g. a GPU overlay), owned dialogs,
// the shell, Softlid.
struct Running { std::wstring app, title, path; };
std::vector<Running> RunningApps();

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
