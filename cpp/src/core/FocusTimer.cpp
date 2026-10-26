#include "core/FocusTimer.h"
#include "core/Clock.h"
#include "core/Lang.h"
#include <algorithm>

namespace bb {

void FocusTimer::Start()
{
    if (state == Focus::Paused) {
        state = Focus::Working;
        return;
    }
    state = Focus::Working;
    left = s.FocusWork * 60;
}

void FocusTimer::Pause()
{
    if (state == Focus::Working) state = Focus::Paused;
}

void FocusTimer::Stop()
{
    state = Focus::Off;
    left = 0;
}

bool FocusTimer::Tick()
{
    if (state != Focus::Working || --left > 0) return false;
    done++;
    state = Focus::Resting;
    return true;
}

void FocusTimer::RestOver(int snoozeSeconds)
{
    if (state != Focus::Resting) return;   // (stopped meanwhile)
    state = Focus::Working;
    left = snoozeSeconds > 0 ? snoozeSeconds : s.FocusWork * 60;
}

float FocusTimer::Progress() const { return 1 - left / static_cast<float>(std::max(1, s.FocusWork * 60)); }

std::wstring FocusTimer::Text() const
{
    if (state == Focus::Off) return MinSec(s.FocusWork * 60);
    if (state == Focus::Resting) return Lang::T(L"Resting");
    return MinSec(left);
}

} // namespace bb
