#pragma once
// A local date and time for the night-schedule tests: a Central European clock (UTC+1, and UTC+2 in summer time,
// which in 2026 runs from 29 March to 25 October; the tests use days well inside or outside it).
#include "core/Clock.h"

inline bb::LocalTime TestTime(int year, int month, int day, int hour, int minute)
{
    bb::LocalTime t;
    t.year = year;
    t.month = month;
    t.day = day;
    t.hour = hour;
    t.minute = minute;
    t.utcOffset = month >= 4 && month <= 10 ? 120 : 60;
    t.zone = L"W. Europe Standard Time";
    return t;
}
