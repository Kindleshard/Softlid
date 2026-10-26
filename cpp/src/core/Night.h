#pragma once
#include "core/Clock.h"
#include "core/Settings.h"
#include <string>
#include <vector>

namespace bb::Night {

// When the blue light filter should be on: sunset to sunrise where you are (from Windows location, or guessed from
// the time zone; computed offline), or fixed hours. It eases in over the fade time after the start and out over the
// fade time before the end. Times are local (platform/Clock gives the time now).

// How far into the night it is: 1 full night, 0 day. An unknown place keeps the filter as set (1).
float Factor(const Settings& s, const LocalTime& now);

// The blue light filter strength (%) at a point of the night (0 day .. 1 full night): the schedule's own night
// strength takes over from the daytime one as the evening eases in, and hands back in the morning. It only ever adds
// warmth: a night strength below the daytime one leaves the daytime one as it is.
double Blue(const Settings& s, float night);

// Two lines for the settings: when the night runs, and where it is in it now.
std::wstring Status(const Settings& s, const LocalTime& now);

// The place the sun times are worked out for, plainly named ("" if none): a city typed or guessed from the time
// zone (Windows' time zone id). fromWindows: the place is coordinates from Windows location.
std::wstring Place(const Settings& s, const std::wstring& zone, bool& fromWindows);

// Cities matching what's typed (ignoring case and accents): those starting with it first, then those containing it.
std::vector<std::wstring> FindCities(const std::wstring& text, size_t max);

std::wstring Coordinates(double lat, double lon);   // "41.01, 28.98", as kept in Settings::City
std::wstring Hm(double minutes);                    // "21:30"

} // namespace bb::Night
