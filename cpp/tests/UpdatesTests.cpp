// The update flow with a fake updater: checking by hand, the weekly check and its date, a newer version found and
// installed, failures - and what the app is told to do each time.
#include "Check.h"
#include "core/Clock.h"
#include "core/Lang.h"
#include "features/update/Updates.h"

using namespace bb;
using Kind = Updater::Result::Kind;
using Look = Updater::State::Look;

namespace {

LocalTime Day(int y, int m, int d)
{
    LocalTime t;
    t.year = y;
    t.month = m;
    t.day = d;
    return t;
}

Updater::Result Result(Kind kind, std::wstring tag = L"")
{
    Updater::Result r{kind, L"(text)"};
    r.release.tag = tag;
    r.release.download = L"https://example.invalid/BlinkBuddy.exe";
    return r;
}

struct Fake {
    int checks = 0, installs = 0;
    Updates updates{[this] { checks++; }, [this](const Updater::Release&) { installs++; }};
};

} // namespace

TEST(DayNumbersAndDates)
{
    CHECK_EQ(DayNumber(1970, 1, 1), 0LL);
    CHECK_EQ(DayNumber(2026, 3, 1) - DayNumber(2026, 2, 28), 1LL);   // not a leap year
    CHECK_EQ(DayNumber(2024, 3, 1) - DayNumber(2024, 2, 28), 2LL);   // a leap year
    CHECK_EQ(DayNumber(2027, 1, 1) - DayNumber(2026, 12, 25), 7LL);
    CHECK_EQ(IsoDate(Day(2026, 10, 26)), std::wstring(L"2026-10-26"));
    int y, m, d;
    CHECK(ParseIsoDate(L"2026-10-26", y, m, d) && y == 2026 && m == 10 && d == 26);
    CHECK(!ParseIsoDate(L"", y, m, d));
    CHECK(!ParseIsoDate(L"2026-13-01", y, m, d));
    CHECK(!ParseIsoDate(L"2026-02-29", y, m, d));   // not a leap year
    CHECK(ParseIsoDate(L"2024-02-29", y, m, d));
    CHECK(!ParseIsoDate(L"2026-04-31", y, m, d));
}

TEST(UpdatesByHand)
{
    Lang::Use(L"en");
    Fake f;
    Settings s;
    f.updates.Check();
    f.updates.Check();   // busy: not twice
    CHECK_EQ(f.checks, 1);
    CHECK(f.updates.State().busy);
    Updates::Effects fx = f.updates.Received(Result(Kind::UpToDate), s, Day(2026, 10, 26));
    CHECK(fx.refresh && !fx.saveSettings && !fx.restart && fx.notifyTitle.empty());
    CHECK(f.updates.State().look == Look::UpToDate);
    CHECK(s.LastUpdateCheck.empty());   // a check by hand doesn't count as the weekly one
    f.updates.Install();                // nothing newer: nothing to install
    CHECK_EQ(f.installs, 0);
}

TEST(UpdatesWeekly)
{
    Lang::Use(L"en");
    Fake f;
    Settings s;
    s.LastUpdateCheck = L"2026-10-20";
    f.updates.Weekly(s, Day(2026, 10, 26));   // six days: not yet
    CHECK_EQ(f.checks, 0);
    f.updates.Weekly(s, Day(2026, 10, 27));   // a week
    CHECK_EQ(f.checks, 1);
    Updates::Effects fx = f.updates.Received(Result(Kind::UpToDate), s, Day(2026, 10, 27));
    CHECK(fx.saveSettings);
    CHECK_EQ(s.LastUpdateCheck, std::wstring(L"2026-10-27"));
    CHECK(f.updates.State().text.empty());   // quiet: nothing to show when there is nothing new
    s.UpdateCheck = false;
    s.LastUpdateCheck.clear();
    f.updates.Weekly(s, Day(2026, 12, 1));   // switched off
    CHECK_EQ(f.checks, 1);
}

TEST(UpdatesNewerFoundAndInstalled)
{
    Lang::Use(L"en");
    Fake f;
    Settings s;
    f.updates.Weekly(s, Day(2026, 10, 26));   // never checked before: due
    Updates::Effects fx = f.updates.Received(Result(Kind::Newer, L"1.1.0"), s, Day(2026, 10, 26));
    CHECK_EQ(fx.notifyTitle, std::wstring(L"BlinkBuddy 1.1.0 is available"));   // the weekly check says so
    CHECK(f.updates.State().look == Look::Newer);
    CHECK(f.updates.State().release.has_value());
    f.updates.Weekly(s, Day(2026, 11, 30));   // a newer one is known: no more checks
    CHECK_EQ(f.checks, 1);
    f.updates.Install();
    CHECK_EQ(f.installs, 1);
    CHECK(f.updates.State().downloading);
    Updater::Result progress = Result(Kind::Progress);
    progress.progress = 0.5f;
    fx = f.updates.Received(progress, s, Day(2026, 11, 30));
    CHECK_NEAR(f.updates.State().progress, 0.5, 1e-6);
    fx = f.updates.Received(Result(Kind::Ready), s, Day(2026, 11, 30));
    CHECK(fx.restart);
}

TEST(UpdatesFailure)
{
    Lang::Use(L"en");
    Fake f;
    Settings s;
    f.updates.Check();
    f.updates.Received(Result(Kind::Newer, L"2.0.0"), s, Day(2026, 10, 26));
    f.updates.Install();
    f.updates.Received(Result(Kind::Failed), s, Day(2026, 10, 26));
    CHECK(!f.updates.State().busy && !f.updates.State().downloading);
    CHECK(f.updates.State().look == Look::Plain);
    f.updates.Install();   // and it can be tried again
    CHECK_EQ(f.installs, 2);
}
