#pragma once
#include "core/Focus.h"
#include "core/Settings.h"
#include <string>

namespace bb {

// The focus timer: work for Settings::FocusWork minutes, then rest for FocusRest, again and again. While it runs, it
// decides when to rest instead of the look-away and stand-up reminders. The app shows the rest (a break card) and
// tells it when the rest is over.
class FocusTimer {
public:
    explicit FocusTimer(const Settings& settings) : s(settings) {}

    Focus State() const { return state; }
    bool Running() const { return state == Focus::Working || state == Focus::Paused; }
    int Left() const { return left; }    // seconds left of the work period
    int Done() const { return done; }    // work periods finished since it was made

    void Start();   // (or resume, when paused)
    void Pause();
    void Stop();

    // Once a second while no break shows: true when a work period has just ended - time for the rest.
    bool Tick();
    // The rest is over: the next work period, or (snoozed) one that long.
    void RestOver(int snoozeSeconds);

    float Progress() const;     // how far into the work period (0..1)
    std::wstring Text() const;  // "24:59", or "Resting"

private:
    const Settings& s;
    Focus state = Focus::Off;
    int left = 0, done = 0;
};

} // namespace bb
