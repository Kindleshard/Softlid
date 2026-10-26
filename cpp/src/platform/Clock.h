#pragma once
#include "core/Clock.h"

namespace bb {

LocalTime LocalNow();       // the local time, with this day's UTC offset and the time zone
std::wstring TimeZoneId();  // the Windows time zone this PC is set to ("W. Europe Standard Time")

} // namespace bb
