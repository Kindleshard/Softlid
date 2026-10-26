#include "platform/Clock.h"
#include <windows.h>

namespace bb {
namespace {

// Local time minus UTC, in minutes, at noon of a day.
double UtcOffsetMinutes(const SYSTEMTIME& day)
{
    SYSTEMTIME noon = day, utc{};
    noon.wHour = 12;
    noon.wMinute = noon.wSecond = noon.wMilliseconds = 0;
    if (!TzSpecificLocalTimeToSystemTime(nullptr, &noon, &utc)) return 0;
    FILETIME a, b;
    SystemTimeToFileTime(&noon, &a);
    SystemTimeToFileTime(&utc, &b);
    auto ticks = [](FILETIME f) { return static_cast<long long>(ULARGE_INTEGER{{f.dwLowDateTime, f.dwHighDateTime}}.QuadPart); };
    return (ticks(a) - ticks(b)) / 600000000.0;
}

} // namespace

LocalTime LocalNow()
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    LocalTime now;
    now.year = t.wYear;
    now.month = t.wMonth;
    now.day = t.wDay;
    now.hour = t.wHour;
    now.minute = t.wMinute;
    now.second = t.wSecond;
    // The zone and the day's offset change rarely, and this is asked often (each tick, mini mode's redraws): worked
    // out again for a new day, or a minute later (the time zone may have been changed in Windows).
    static int cachedDay = -1;
    static ULONGLONG cachedAt = 0;
    static double offset = 0;
    static std::wstring zone;
    int day = t.wYear * 512 + t.wMonth * 32 + t.wDay;
    ULONGLONG tick = GetTickCount64();
    if (day != cachedDay || tick - cachedAt >= 60000) {
        cachedDay = day;
        cachedAt = tick;
        offset = UtcOffsetMinutes(t);
        zone = TimeZoneId();
    }
    now.utcOffset = offset;
    now.zone = zone;
    return now;
}

std::wstring TimeZoneId()
{
    DYNAMIC_TIME_ZONE_INFORMATION tz{};
    GetDynamicTimeZoneInformation(&tz);
    return tz.TimeZoneKeyName;
}

} // namespace bb
