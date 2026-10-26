#pragma once
#include <string>

namespace bb {

// A local date and time, as the night schedule needs them. platform/Clock reads them from the system; tests make
// their own, for any day and place.
struct LocalTime {
    int year = 2000, month = 1, day = 1;   // month 1..12
    int hour = 0, minute = 0, second = 0;
    double utcOffset = 0;   // minutes, local time minus UTC, at noon that day (so a daylight saving switch that
                            // night doesn't count)
    std::wstring zone;      // the system's time zone ("W. Europe Standard Time"), to guess a city when none is set
};

// A count of seconds as "m:ss" ("19:05"; below zero shows as 0:00).
inline std::wstring MinSec(int seconds)
{
    seconds = seconds < 0 ? 0 : seconds;
    std::wstring ss = std::to_wstring(seconds % 60);
    return std::to_wstring(seconds / 60) + L":" + (ss.size() < 2 ? L"0" + ss : ss);
}

} // namespace bb
