// The seams between core and platform: settings as text, shortcuts as Windows key codes, city search, and the night
// schedule in a time zone other than this PC's.
#include "Check.h"
#include "core/Night.h"
#include "core/Settings.h"
#include "platform/Keys.h"
#include "platform/Text.h"

using namespace bb;

// The first line where two settings files differ ("" if none).
static std::wstring FirstDifference(const std::wstring& a, const std::wstring& b)
{
    std::vector<std::wstring> x = SplitLines(a), y = SplitLines(b);
    for (size_t i = 0; i < std::max(x.size(), y.size()); i++) {
        std::wstring l = i < x.size() ? x[i] : L"", r = i < y.size() ? y[i] : L"";
        if (l != r) return l + L" / " + r;
    }
    return L"";
}

TEST(SettingsRoundTrip)
{
    Settings s;
    s.Brightness = 64;
    s.ScheduleEnabled = true;
    s.City = L"İzmir";
    s.Hotkeys = L"1=Ctrl+Alt+G";
    // Loading tidies a few values (the strip colour 0, an old palette index, becomes black written out as ARGB), so
    // what must hold is that a loaded file reads back unchanged.
    Settings once = Settings::FromText(s.ToText()), twice = Settings::FromText(once.ToText());
    CHECK_EQ(FirstDifference(twice.ToText(), once.ToText()), std::wstring(L""));
    CHECK_EQ(FirstDifference(once.ToText(), s.ToText()), std::wstring(L"StripColor=-16777216 / StripColor=0"));
    CHECK_EQ(once.Brightness, 64);
    CHECK_EQ(once.City, std::wstring(L"İzmir"));
    CHECK_EQ(once.Hotkeys, std::wstring(L"1=Ctrl+Alt+G"));
}

TEST(SettingsReadOldAndOddFiles)
{
    // An old file: the schedule without a night strength of its own, a value out of range, a line that isn't one,
    // and spaces around the equals sign.
    Settings s = Settings::FromText(L"ScheduleEnabled=True\r\nBlueFilter=40\r\nBrightness=5\r\nnonsense\r\n FilterEnabled = False \r\n");
    CHECK_EQ(s.NightBlue, 40);   // the old blue light filter became the night strength
    CHECK_EQ(s.BlueFilter, 0);   // and days stay neutral
    CHECK_EQ(s.Brightness, 20);  // held to its range
    CHECK(!s.FilterEnabled);
    CHECK_EQ(Settings::FromText(L"Brightness=abc").Brightness, Settings{}.Brightness);   // unreadable: the default
}

TEST(KeysParseAndDescribe)
{
    UINT mods = 0, vk = 0;
    CHECK(Keys::Parse(L"Ctrl+Alt+Shift+F", mods, vk));
    CHECK_EQ(mods, static_cast<UINT>(MOD_CONTROL | MOD_ALT | MOD_SHIFT));
    CHECK_EQ(vk, static_cast<UINT>('F'));
    CHECK(Keys::Parse(L"Ctrl+Alt+Oemplus", mods, vk));
    CHECK_EQ(vk, static_cast<UINT>(VK_OEM_PLUS));
    CHECK(!Keys::Parse(L"F", mods, vk));            // a shortcut needs a modifier
    CHECK(!Keys::Parse(L"Ctrl+Nope", mods, vk));    // and a key that exists
    CHECK_EQ(Keys::Describe(VK_UP, true, true, false), std::wstring(L"Ctrl+Alt+Up"));
    CHECK_EQ(Keys::Describe(VK_CONTROL, true, false, false), std::wstring(L""));   // only a modifier so far
    CHECK_EQ(Keys::Describe('5', false, true, false), std::wstring(L"Alt+D5"));
}

TEST(CitySearchIgnoresCaseAndAccents)
{
    CHECK_EQ(FoldForSearch(L"  İzmir "), std::wstring(L"izmir"));
    CHECK_EQ(FoldForSearch(L"São Paulo"), std::wstring(L"sao paulo"));
    auto found = Night::FindCities(L"IZMIR", 6);
    CHECK(!found.empty() && found[0] == L"Izmir");   // (the city list writes it without the dotted capital)
}

TEST(NightInAnotherTimeZone)
{
    // Istanbul on its own clock (UTC+3): sunset 20:39, sunrise 05:32 at midsummer.
    Settings s;
    s.ScheduleEnabled = true;
    s.City = L"41.01, 28.98";
    LocalTime t;
    t.year = 2026;
    t.month = 6;
    t.day = 21;
    t.hour = 12;
    t.utcOffset = 180;
    std::wstring status = Night::Status(s, t);
    CHECK(status.starts_with(L"Sunset 20:39, sunrise 05:32"));
    // No place set: the city of the time zone stands in.
    s.City.clear();
    t.zone = L"Turkey Standard Time";
    CHECK(Night::Status(s, t).find(L"Istanbul (from your time zone)") != std::wstring::npos);
    bool fromWindows = true;
    CHECK_EQ(Night::Place(s, L"Turkey Standard Time", fromWindows), std::wstring(L"Istanbul"));
    CHECK(!fromWindows);
}
