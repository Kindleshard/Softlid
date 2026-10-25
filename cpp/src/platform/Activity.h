#pragma once
#include <windows.h>

namespace bb {

// What the person at the PC is doing, as far as reminders care.
int IdleSeconds();            // since the last keyboard or mouse input
bool FullscreenAppRunning();  // a game, video or presentation fills the screen (reminders wait for it to end)

// Monitors: the one under the pointer (usually the one being looked at), and its scale.
RECT WorkAreaAtCursor();
float ScaleAtCursor();        // 1.0 at 100% display scaling
float ScaleOf(HWND window);
float ScaleAt(POINT p);       // of the monitor at a point

} // namespace bb
