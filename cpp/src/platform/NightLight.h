#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>

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

// Reads both values (two small registry reads). Windows changes them without the registry's change notifications
// firing (tested: a watch on the keys, or on the whole store, stays silent), so they are simply read again each
// second.
State Read();
State Parse(const std::vector<uint8_t>& settings, const std::vector<uint8_t>& state);   // (Read's decoding, for testing)

// Softlid's blue light filter (%) for a colour temperature.
int BlueFor(int kelvin);

} // namespace bb::NightLight
