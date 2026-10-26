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
    now.utcOffset = UtcOffsetMinutes(t);
    now.zone = TimeZoneId();
    return now;
}

std::wstring TimeZoneId()
{
    DYNAMIC_TIME_ZONE_INFORMATION tz{};
    GetDynamicTimeZoneInformation(&tz);
    return tz.TimeZoneKeyName;
}

} // namespace bb
