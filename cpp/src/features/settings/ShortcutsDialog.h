#pragma once
#include <windows.h>

namespace bb {

class App;

namespace ShortcutsDialog {

// The keyboard shortcuts, each changeable: click its box and press the keys (with Ctrl, Alt and/or Shift); Backspace
// or Delete leaves the action without one. Keys another program already uses are marked in red. While it is open the
// shortcuts are off, so pressing one records it instead of doing it; the owner is disabled meanwhile.
void Open(App& app, HWND owner, bool activate = true);   // activate false: diagnostics (no focus taken)

} // namespace ShortcutsDialog
} // namespace bb
