#pragma once
// A local date and time for the night-schedule tests, in the shape the night code takes.
#include <windows.h>

inline SYSTEMTIME TestTime(int year, int month, int day, int hour, int minute)
{
    SYSTEMTIME t{};
    t.wYear = static_cast<WORD>(year);
    t.wMonth = static_cast<WORD>(month);
    t.wDay = static_cast<WORD>(day);
    t.wHour = static_cast<WORD>(hour);
    t.wMinute = static_cast<WORD>(minute);
    return t;
}
