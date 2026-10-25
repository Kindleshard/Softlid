#pragma once
#include <windows.h>

namespace bb::Backlight {

// Sets the real backlight: external monitors over DDC/CI, a laptop's built-in panel through WMI. Each round-trip
// takes tens of milliseconds, so the work runs on a worker thread and rapid slider moves collapse into the latest
// value.
void Set(int percent);

// Asks every display whether it answers, on a worker thread, then posts `message` to `window` with
// wParam = MAKEWPARAM(displays that answered, displays in total) and lParam = the first answering display's
// brightness (%), or -1.
void Probe(HWND window, UINT message);

} // namespace bb::Backlight
