#pragma once
#include <windows.h>

namespace bb::KeepAwake {

// Keeping the PC awake: Windows is told the system and the display are in use, so it neither sleeps nor turns the
// screen off, until this is switched off again (or Softlid exits).
inline void Set(bool on)
{
    static bool now = false;
    if (on == now) return;
    now = on;
    SetThreadExecutionState(on ? ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED : ES_CONTINUOUS);
}

} // namespace bb::KeepAwake
