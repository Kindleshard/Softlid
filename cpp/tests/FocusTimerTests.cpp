// The focus timer: work, rest, again; pausing; snoozing the rest; stopping; what it shows.
#include "Check.h"
#include "core/Clock.h"
#include "core/FocusTimer.h"
#include "core/Lang.h"

using namespace bb;

namespace {

// Ticks until a work period ends (or the limit is reached); how many ticks it took.
int TicksToRest(FocusTimer& t, int limit = 100000)
{
    for (int i = 1; i <= limit; i++)
        if (t.Tick()) return i;
    return -1;
}

} // namespace

TEST(FocusWorkThenRest)
{
    Settings s;
    s.FocusWork = 25;
    FocusTimer t(s);
    CHECK(t.State() == Focus::Off);
    CHECK(!t.Tick());   // off: nothing happens
    t.Start();
    CHECK(t.State() == Focus::Working);
    CHECK_EQ(t.Left(), 25 * 60);
    CHECK_EQ(TicksToRest(t), 25 * 60);
    CHECK(t.State() == Focus::Resting);
    CHECK_EQ(t.Done(), 1);
    CHECK(!t.Tick());   // resting: the app shows the rest; ticks change nothing
    t.RestOver(0);
    CHECK(t.State() == Focus::Working);
    CHECK_EQ(t.Left(), 25 * 60);
}

TEST(FocusPauseResumeStop)
{
    Settings s;
    s.FocusWork = 1;
    FocusTimer t(s);
    t.Start();
    for (int i = 0; i < 10; i++) t.Tick();
    t.Pause();
    CHECK(t.State() == Focus::Paused);
    for (int i = 0; i < 100; i++) CHECK(!t.Tick());
    CHECK_EQ(t.Left(), 50);   // paused: the time stays
    t.Start();                // resumes where it was
    CHECK_EQ(t.Left(), 50);
    CHECK_EQ(TicksToRest(t), 50);
    t.Stop();
    CHECK(t.State() == Focus::Off);
    t.RestOver(0);            // a rest card closing after a stop doesn't restart it
    CHECK(t.State() == Focus::Off);
}

TEST(FocusSnoozedRest)
{
    Settings s;
    FocusTimer t(s);
    t.Start();
    TicksToRest(t);
    t.RestOver(300);          // "snooze 5 minutes": a short work period, then rest again
    CHECK_EQ(t.Left(), 300);
    CHECK_EQ(TicksToRest(t), 300);
    CHECK_EQ(t.Done(), 2);
}

TEST(FocusProgressAndText)
{
    Lang::Use(L"en");
    Settings s;
    s.FocusWork = 10;
    FocusTimer t(s);
    CHECK_EQ(t.Text(), std::wstring(L"10:00"));   // off: the period it would run
    t.Start();
    CHECK_NEAR(t.Progress(), 0, 1e-6);
    for (int i = 0; i < 150; i++) t.Tick();
    CHECK_NEAR(t.Progress(), 0.25, 1e-6);
    CHECK_EQ(t.Text(), std::wstring(L"7:30"));
    TicksToRest(t);
    CHECK_EQ(t.Text(), std::wstring(L"Resting"));
    CHECK_EQ(MinSec(65), std::wstring(L"1:05"));
    CHECK_EQ(MinSec(-3), std::wstring(L"0:00"));
    CHECK_EQ(MinSec(3600), std::wstring(L"60:00"));
}
