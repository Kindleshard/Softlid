#include "core/Reminders.h"
#include <algorithm>

namespace bb {

Reminders::Due Reminders::Tick(int idleSeconds, bool fullscreen, bool standUpTimer)
{
    // Away from the PC long enough: that was a break.
    if (idleSeconds >= AwaySeconds) {
        breakElapsed = 0;
        blinkLeft = s.BlinkInterval;
        if (idleSeconds >= s.LongBreakDuration * 60) longElapsed = 0;
        return Due::Nothing;
    }
    if (standUpTimer && s.LongBreakEnabled && ++longElapsed >= s.LongBreakInterval * 60) {
        longElapsed = s.LongBreakInterval * 60;   // (waits there while a full-screen app runs)
        if (!fullscreen) return Due::StandUp;
    }
    if (s.BreakEnabled && ++breakElapsed >= s.BreakInterval * 60) {
        breakElapsed = s.BreakInterval * 60;
        // A stand-up break is moments away; let it cover this one (not while the focus timer holds that countdown).
        if (standUpTimer && s.LongBreakEnabled && s.LongBreakInterval * 60 - longElapsed <= 120) breakElapsed = 0;
        else if (!fullscreen) return Due::LookAway;
    }
    if (s.BlinkEnabled && --blinkLeft <= 0) {
        blinkLeft = s.BlinkInterval;
        if (!fullscreen) return Due::Blink;
    }
    return Due::Nothing;
}

void Reminders::BreakFinished(bool standUp, int snoozeSeconds)
{
    if (standUp) {
        longElapsed = snoozeSeconds > 0 ? std::max(0, s.LongBreakInterval * 60 - snoozeSeconds) : 0;
        if (snoozeSeconds == 0) breakElapsed = 0;   // standing up was a look-away break too
    } else {
        breakElapsed = snoozeSeconds > 0 ? std::max(0, s.BreakInterval * 60 - snoozeSeconds) : 0;
    }
    blinkLeft = s.BlinkInterval;
}

float Reminders::BreakProgress() const
{
    return std::min(1.0f, breakElapsed / static_cast<float>(std::max(1, s.BreakInterval * 60)));
}

} // namespace bb
