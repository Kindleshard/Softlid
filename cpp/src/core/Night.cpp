#include "core/Night.h"
#include "core/Lang.h"
#include "platform/Text.h"
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <numbers>

namespace bb::Night {
namespace {

// Name, latitude, longitude, and the Windows time zone whose city it is (to guess the place when none is known).
struct City { const wchar_t* name; double lat, lon; const wchar_t* zone; };

constexpr City Cities[] = {
    {L"Istanbul", 41.01, 28.98, L"Turkey Standard Time"}, {L"Ankara", 39.93, 32.86, L""}, {L"Izmir", 38.42, 27.14, L""},
    {L"Bursa", 40.19, 29.06, L""}, {L"Antalya", 36.90, 30.70, L""}, {L"Adana", 37.00, 35.32, L""},
    {L"Konya", 37.87, 32.48, L""}, {L"Gaziantep", 37.07, 37.38, L""}, {L"Kayseri", 38.72, 35.48, L""},
    {L"Eskisehir", 39.78, 30.52, L""}, {L"Samsun", 41.29, 36.33, L""}, {L"Trabzon", 41.00, 39.72, L""},
    {L"Diyarbakir", 37.91, 40.24, L""}, {L"Erzurum", 39.90, 41.27, L""},
    {L"London", 51.51, -0.13, L"GMT Standard Time"}, {L"Dublin", 53.35, -6.26, L""}, {L"Lisbon", 38.72, -9.14, L""},
    {L"Madrid", 40.42, -3.70, L""}, {L"Barcelona", 41.39, 2.17, L""},
    {L"Paris", 48.86, 2.35, L"Romance Standard Time"}, {L"Brussels", 50.85, 4.35, L""},
    {L"Amsterdam", 52.37, 4.90, L""}, {L"Berlin", 52.52, 13.40, L"W. Europe Standard Time"},
    {L"Munich", 48.14, 11.58, L""}, {L"Hamburg", 53.55, 9.99, L""}, {L"Frankfurt", 50.11, 8.68, L""},
    {L"Zurich", 47.38, 8.54, L""}, {L"Vienna", 48.21, 16.37, L""}, {L"Rome", 41.90, 12.50, L""},
    {L"Milan", 45.46, 9.19, L""}, {L"Prague", 50.08, 14.44, L""},
    {L"Warsaw", 52.23, 21.01, L"Central European Standard Time"},
    {L"Budapest", 47.50, 19.04, L"Central Europe Standard Time"}, {L"Stockholm", 59.33, 18.07, L""},
    {L"Oslo", 59.91, 10.75, L""}, {L"Copenhagen", 55.68, 12.57, L""}, {L"Helsinki", 60.17, 24.94, L""},
    {L"Athens", 37.98, 23.73, L"GTB Standard Time"}, {L"Sofia", 42.70, 23.32, L""},
    {L"Bucharest", 44.43, 26.10, L""}, {L"Belgrade", 44.79, 20.45, L""},
    {L"Kyiv", 50.45, 30.52, L"FLE Standard Time"}, {L"Moscow", 55.76, 37.62, L"Russian Standard Time"},
    {L"Baku", 40.41, 49.87, L"Azerbaijan Standard Time"}, {L"Tbilisi", 41.72, 44.78, L"Georgian Standard Time"},
    {L"Nicosia", 35.19, 33.38, L""}, {L"Tel Aviv", 32.09, 34.78, L"Israel Standard Time"},
    {L"Cairo", 30.04, 31.24, L"Egypt Standard Time"}, {L"Riyadh", 24.71, 46.68, L"Arab Standard Time"},
    {L"Dubai", 25.20, 55.27, L"Arabian Standard Time"}, {L"Tehran", 35.69, 51.39, L"Iran Standard Time"},
    {L"Karachi", 24.86, 67.01, L"Pakistan Standard Time"}, {L"Delhi", 28.61, 77.21, L"India Standard Time"},
    {L"Mumbai", 19.08, 72.88, L""}, {L"Bangalore", 12.97, 77.59, L""},
    {L"Dhaka", 23.81, 90.41, L"Bangladesh Standard Time"}, {L"Bangkok", 13.76, 100.50, L"SE Asia Standard Time"},
    {L"Singapore", 1.35, 103.82, L"Singapore Standard Time"}, {L"Jakarta", -6.21, 106.85, L""},
    {L"Manila", 14.60, 120.98, L""}, {L"Hong Kong", 22.32, 114.17, L""},
    {L"Beijing", 39.90, 116.40, L"China Standard Time"}, {L"Shanghai", 31.23, 121.47, L""},
    {L"Seoul", 37.57, 126.98, L"Korea Standard Time"}, {L"Tokyo", 35.68, 139.69, L"Tokyo Standard Time"},
    {L"Sydney", -33.87, 151.21, L"AUS Eastern Standard Time"}, {L"Melbourne", -37.81, 144.96, L""},
    {L"Perth", -31.95, 115.86, L"W. Australia Standard Time"},
    {L"Auckland", -36.85, 174.76, L"New Zealand Standard Time"},
    {L"Johannesburg", -26.20, 28.05, L"South Africa Standard Time"},
    {L"Nairobi", -1.29, 36.82, L"E. Africa Standard Time"}, {L"Lagos", 6.52, 3.38, L"W. Central Africa Standard Time"},
    {L"Toronto", 43.65, -79.38, L""}, {L"Montreal", 45.50, -73.57, L""}, {L"Vancouver", 49.28, -123.12, L""},
    {L"New York", 40.71, -74.01, L"Eastern Standard Time"}, {L"Boston", 42.36, -71.06, L""},
    {L"Washington", 38.91, -77.04, L""}, {L"Miami", 25.76, -80.19, L""}, {L"Atlanta", 33.75, -84.39, L""},
    {L"Chicago", 41.88, -87.63, L"Central Standard Time"}, {L"Dallas", 32.78, -96.80, L""},
    {L"Houston", 29.76, -95.37, L""}, {L"Denver", 39.74, -104.99, L"Mountain Standard Time"},
    {L"Phoenix", 33.45, -112.07, L"US Mountain Standard Time"},
    {L"Los Angeles", 34.05, -118.24, L"Pacific Standard Time"}, {L"San Francisco", 37.77, -122.42, L""},
    {L"Seattle", 47.61, -122.33, L""}, {L"Mexico City", 19.43, -99.13, L"Central Standard Time (Mexico)"},
    {L"Bogota", 4.71, -74.07, L"SA Pacific Standard Time"}, {L"Lima", -12.05, -77.04, L""},
    {L"Santiago", -33.45, -70.67, L"Pacific SA Standard Time"},
    {L"Buenos Aires", -34.60, -58.38, L"Argentina Standard Time"},
    {L"Sao Paulo", -23.55, -46.63, L"E. South America Standard Time"}, {L"Rio de Janeiro", -22.91, -43.17, L""},
};

double Mod(double a, double m) { return std::fmod(std::fmod(a, m) + m, m); }

// "41.01, 28.98" (or "41.01 28.98"): coordinates, as Windows location writes them.
bool ParseCoordinates(const std::wstring& text, double& lat, double& lon)
{
    wchar_t rest[2] = {};
    std::wstring t = text;
    for (wchar_t& c : t) if (c == L',' || c == L';') c = L' ';
    return swscanf_s(t.c_str(), L"%lf %lf %1ls", &lat, &lon, rest, 2) == 2;
}

const City* CityOfTimeZone(const std::wstring& zone)
{
    for (const City& c : Cities) if (zone == c.zone) return &c;
    return nullptr;
}

const City* CityNamed(const std::wstring& text)
{
    std::wstring key = FoldForSearch(text);
    for (const City& c : Cities) if (FoldForSearch(c.name) == key) return &c;
    return nullptr;
}

// Where the sun times are for, and its name for the status line. zone: the time zone, for when no place is set.
bool Resolve(const std::wstring& city, const std::wstring& zone, double& lat, double& lon, std::wstring& name)
{
    // (Looked up again only when the text or the time zone changes: the status asks each second.)
    static std::wstring cachedText = L"\x2";   // (matches no text and zone)
    static bool cachedOk;
    static double cachedLat, cachedLon;
    static std::wstring cachedName;
    std::wstring text = Trim(city), key = text + L"" + zone;
    if (key != cachedText) {
        cachedText = key;
        cachedOk = false;
        if (text.empty()) {
            if (const City* c = CityOfTimeZone(zone)) {
                cachedLat = c->lat;
                cachedLon = c->lon;
                cachedName = Lang::F(L"{0} (from your time zone)", {c->name});
                cachedOk = true;
            }
        } else if (ParseCoordinates(text, cachedLat, cachedLon) && std::abs(cachedLat) <= 90 && std::abs(cachedLon) <= 180) {
            cachedName = Lang::T(L"your location (from Windows)");
            cachedOk = true;
        } else if (const City* c = CityNamed(text)) {
            cachedLat = c->lat;
            cachedLon = c->lon;
            cachedName = c->name;
            cachedOk = true;
        }
    }
    lat = cachedLat;
    lon = cachedLon;
    name = cachedName;
    return cachedOk;
}

int DayOfYear(const LocalTime& t)
{
    static const int before[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    bool leap = (t.year % 4 == 0 && t.year % 100 != 0) || t.year % 400 == 0;
    return before[t.month - 1] + t.day + (leap && t.month > 2 ? 1 : 0);
}

enum class Sky { Normal, NoNight, NoDay, Unknown };

// NOAA's approximation, accurate to a minute or two. Times are local minutes after midnight.
Sky SunTimes(const LocalTime& day, double lat, double lon, double& rise, double& set)
{
    constexpr double pi = std::numbers::pi;
    double g = 2 * pi / 365 * (DayOfYear(day) - 1);
    double eq = 229.18 * (0.000075 + 0.001868 * std::cos(g) - 0.032077 * std::sin(g) - 0.014615 * std::cos(2 * g) - 0.040849 * std::sin(2 * g));
    double decl = 0.006918 - 0.399912 * std::cos(g) + 0.070257 * std::sin(g) - 0.006758 * std::cos(2 * g)
                + 0.000907 * std::sin(2 * g) - 0.002697 * std::cos(3 * g) + 0.00148 * std::sin(3 * g);
    double phi = lat * pi / 180;
    double cosHa = std::cos(90.833 * pi / 180) / (std::cos(phi) * std::cos(decl)) - std::tan(phi) * std::tan(decl);
    if (cosHa < -1) return Sky::NoNight;
    if (cosHa > 1) return Sky::NoDay;
    double ha = std::acos(cosHa) * 180 / pi, offset = day.utcOffset;
    rise = Mod(720 - 4 * (lon + ha) - eq + offset, 1440);
    set = Mod(720 - 4 * (lon - ha) - eq + offset, 1440);
    return Sky::Normal;
}

// The night window in minutes after local midnight.
Sky Window(const Settings& s, const LocalTime& now, double& start, double& end, std::wstring& where)
{
    start = s.NightFrom;
    end = s.NightTo;
    if (s.ScheduleMode == 1) return Sky::Normal;
    double lat, lon;
    if (!Resolve(s.City, now.zone, lat, lon, where)) return Sky::Unknown;
    return SunTimes(now, lat, lon, end, start);   // the night runs from sunset to sunrise
}

void Position(const LocalTime& now, double start, double end, double& into, double& len)
{
    len = Mod(end - start, 1440);
    into = Mod(now.hour * 60 + now.minute + now.second / 60.0 - start, 1440);
}

double FadeMinutes(const Settings& s, double len) { return std::min<double>(s.NightFade, len / 2); }

} // namespace

double Blue(const Settings& s, float night) { return s.BlueFilter + std::max(0, s.NightBlue - s.BlueFilter) * night; }

float Factor(const Settings& s, const LocalTime& now)
{
    double start, end, into, len;
    std::wstring where;
    Sky sky = Window(s, now, start, end, where);
    if (sky == Sky::Unknown || sky == Sky::NoDay) return 1;   // unknown place: keep the filter as set
    if (sky == Sky::NoNight) return 0;
    Position(now, start, end, into, len);
    if (into >= len) return 0;
    double fade = FadeMinutes(s, len);
    if (fade <= 0) return 1;
    // Smoothstep, so the change starts and ends gently instead of at a constant rate.
    double t = std::min(1.0, std::min(into, len - into) / fade);
    return static_cast<float>(t * t * (3 - 2 * t));
}

std::wstring Status(const Settings& s, const LocalTime& now)
{
    if (!s.ScheduleEnabled) return Lang::T(L"Off: the blue light filter stays as set on the Screen tab.");
    double start, end, into, len;
    std::wstring where;
    switch (Window(s, now, start, end, where)) {
    case Sky::Unknown:
        return Lang::T(L"Windows location is off and your time zone isn't one BlinkBuddy knows, so it can't tell when the sun sets. "
                       L"Turn on location for desktop apps in Windows Settings, or use custom hours.");
    case Sky::NoNight: return Lang::F(L"The sun doesn't set today in {0}, so the filter rests.", {where});
    case Sky::NoDay: return Lang::F(L"The sun doesn't rise today in {0}, so the filter stays on.", {where});
    case Sky::Normal: break;
    }
    std::wstring head = s.ScheduleMode == 0 ? Lang::F(L"Sunset {0}, sunrise {1} in {2}.", {Hm(start), Hm(end), where})
                                            : Lang::F(L"On from {0} to {1}.", {Hm(start), Hm(end)});
    Position(now, start, end, into, len);
    double fade = FadeMinutes(s, len);
    std::wstring percent = std::to_wstring(std::lround(Factor(s, now) * 100));
    std::wstring state = into >= len ? Lang::F(L"Resting until {0}.", {Hm(start)})
        : into < fade ? Lang::F(L"Easing in: {0:0}% of the way, full at {1}.", {percent, Hm(start + fade)})
        : len - into < fade ? Lang::F(L"Easing out: back to normal at {0}.", {Hm(end)})
        : Lang::F(L"Full strength until {0}, then easing out until {1}.", {Hm(end - fade), Hm(end)});
    return head + L"\n" + state;
}

std::wstring Place(const Settings& s, const std::wstring& zone, bool& fromWindows)
{
    std::wstring text = Trim(s.City);
    double lat, lon;
    fromWindows = ParseCoordinates(text, lat, lon);
    if (fromWindows) return L"";
    const City* c = text.empty() ? CityOfTimeZone(zone) : CityNamed(text);
    return c ? c->name : L"";
}

std::vector<std::wstring> FindCities(const std::wstring& text, size_t max)
{
    std::wstring key = FoldForSearch(text);
    std::vector<std::wstring> starts, contains;
    if (key.empty()) return starts;
    for (const City& c : Cities) {
        std::wstring name = FoldForSearch(c.name);
        if (name.starts_with(key)) starts.push_back(c.name);
        else if (name.find(key) != std::wstring::npos) contains.push_back(c.name);
    }
    starts.insert(starts.end(), contains.begin(), contains.end());
    if (starts.size() > max) starts.resize(max);
    return starts;
}

std::wstring Coordinates(double lat, double lon)
{
    wchar_t text[48];
    swprintf_s(text, L"%.2f, %.2f", lat, lon);   // (always a point: the C runtime's "C" locale)
    return text;
}

std::wstring Hm(double minutes)
{
    int m = static_cast<int>(std::lround(Mod(minutes, 1440))) % 1440;
    wchar_t text[8];
    swprintf_s(text, L"%02d:%02d", m / 60, m % 60);
    return text;
}

} // namespace bb::Night
