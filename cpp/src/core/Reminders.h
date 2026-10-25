#pragma once
#include "core/Settings.h"

namespace bb {

// When reminders fall due: counts the seconds to the next blink, look-away break and stand-up break. Plain logic
// with no windows - the app asks it once a second and shows whatever is due.
class Reminders {
public:
    enum class Due { Nothing, Blink, LookAway, StandUp };

    // This long without keyboard or mouse input counts as a break.
    static constexpr int AwaySeconds = 300;

    explicit Reminders(const Settings& settings) : s(settings), blinkLeft(settings.BlinkInterval) {}

    // One second has passed (not called while paused or while a break shows). idleSeconds: since the last input.
    // fullscreen: a game or video fills the screen - breaks that fall due wait for it to end. breakTimers: off while
    // the focus timer decides when to rest instead.
    Due Tick(int idleSeconds, bool fullscreen, bool breakTimers = true);

    // A break ended, or was snoozed for that many seconds (0: taken or skipped).
    void BreakFinished(bool standUp, int snoozeSeconds);
    void ResetBlink() { blinkLeft = s.BlinkInterval; }
    void BlinkIntervalChanged() { if (blinkLeft > s.BlinkInterval) blinkLeft = s.BlinkInterval; }

    int SecondsToBreak() const { return s.BreakInterval * 60 - breakElapsed; }
    int SecondsToStandUp() const { return s.LongBreakInterval * 60 - longElapsed; }
    float BreakProgress() const;   // 0..1 of the wait for the next look-away break

private:
    const Settings& s;
    int blinkLeft;
    int breakElapsed = 0, longElapsed = 0;
};

} // namespace bb
