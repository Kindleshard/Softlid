#pragma once
#include <cstdio>
#include <cwchar>
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

// Days since 1 January 1970 for a calendar date (any date, leap years included).
inline long long DayNumber(int year, int month, int day)
{
    year -= month <= 2;
    long long era = (year >= 0 ? year : year - 399) / 400;
    long long yoe = year - era * 400, doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}

// A date as "2026-10-26", and back (false if it isn't one).
inline std::wstring IsoDate(const LocalTime& t)
{
    wchar_t text[16];
    swprintf_s(text, L"%04d-%02d-%02d", t.year, t.month, t.day);
    return text;
}
inline bool ParseIsoDate(const std::wstring& text, int& year, int& month, int& day)
{
    // (by hand rather than swscanf, which would bring the C runtime's whole reading engine into the exe)
    const wchar_t* at = text.c_str();
    wchar_t* end = nullptr;
    int* parts[] = {&year, &month, &day};
    for (int i = 0; i < 3; i++) {
        *parts[i] = static_cast<int>(std::wcstol(at, &end, 10));
        if (end == at || (i < 2 && *end != L'-')) return false;
        at = end + 1;
    }
    if (month < 1 || month > 12 || day < 1) return false;
    long long daysInMonth = DayNumber(month == 12 ? year + 1 : year, month == 12 ? 1 : month + 1, 1) - DayNumber(year, month, 1);
    return day <= daysInMonth;   // (not 31 February)
}

// A count of seconds as "m:ss" ("19:05"; below zero shows as 0:00).
inline std::wstring MinSec(int seconds)
{
    seconds = seconds < 0 ? 0 : seconds;
    std::wstring ss = std::to_wstring(seconds % 60);
    return std::to_wstring(seconds / 60) + L":" + (ss.size() < 2 ? L"0" + ss : ss);
}

} // namespace bb
