#pragma once
#include <windows.h>
#include <functional>

namespace bb::CloseDialog {

// Asked when the settings window is closed (if Options says to ask): keep Softlid running in the tray, so the
// reminders and the screen filter go on, or exit it. The owner is disabled while it shows; answer gets 0 (cancelled),
// 1 (tray) or 2 (exit), and whether to remember the choice.
void Ask(HWND owner, std::function<void(int choice, bool remember)> answer);

} // namespace bb::CloseDialog
