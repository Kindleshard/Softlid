#pragma once
#include <windows.h>

namespace bb::NightLight {

// Windows' own Night light, read (never written) from where Windows keeps it: two small binary values in the
// registry, in an undocumented format decoded in docs/NIGHTLIGHT.md. Anything unexpected reads as "not found".

struct State {
    bool found = false;       // the settings were there and made sense
    bool on = false;          // warming the screen right now
    bool byHand = false;      // turned on or off with "Turn on now", not by the schedule
    bool scheduled = false;   // "Schedule night light" is on
    bool setHours = false;    // the schedule is "Set hours" (otherwise sunset to sunrise)
    int from = -1, to = -1;   // Set hours, in minutes after midnight
    int sunset = -1, sunrise = -1;   // as Windows worked them out
    int kelvin = 0;           // the strength, as a colour temperature (6500 K = none)

    int Strength() const;     // as the slider in Windows Settings shows it (0..100)
    int Start() const { return setHours ? from : sunset; }   // when the scheduled night starts and ends
    int End() const { return setHours ? to : sunrise; }
    bool operator==(const State&) const = default;
};

State Read();

// BlinkBuddy's blue light filter (%) for a colour temperature.
int BlueFor(int kelvin);

// Tells `window` (as `message`) when Night light's settings or state change. After each message, Rearm.
class Watcher {
public:
    Watcher() = default;
    ~Watcher();
    Watcher(const Watcher&) = delete;
    Watcher& operator=(const Watcher&) = delete;

    // The message carries the key that changed in wParam: pass it to Rearm, to hear of its next change.
    void Start(HWND window, UINT message);   // again later if Night light wasn't set up yet (nothing to watch)
    void Rearm(int key);
    bool Watching() const { return keys[0] || keys[1]; }

private:
    void Stop();
    HKEY keys[2] = {};
    HANDLE events[2] = {}, waits[2] = {};
    struct Slot { Watcher* owner; int key; } slots[2] = {{this, 0}, {this, 1}};
    HWND window = nullptr;
    UINT message = 0;
};

} // namespace bb::NightLight
