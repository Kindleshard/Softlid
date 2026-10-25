#pragma once
#include <windows.h>

namespace bb::Location {

// Asks Windows where this PC is, on a worker thread (Windows may take a while to work it out from Wi-Fi). It answers
// only if location is allowed for desktop apps. The answer, rounded to about a kilometre, is posted to `window` as
// `message` with a heap-allocated Fix in lParam, which the receiver deletes; ok is false if Windows couldn't tell.
struct Fix { bool ok; double lat, lon; };

void Locate(HWND window, UINT message);

} // namespace bb::Location
