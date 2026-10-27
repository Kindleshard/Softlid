// The core logic: colour maths, tone curves, the visibility guard, the night schedule, modes, shortcuts, reminders
// and the guided-break timing. Obvious answers are checked as such; everything else is pinned to the values the code
// gave when these tests were written (CHECK_GOLDEN), so a change in behaviour can't slip through a refactor.
#include "Check.h"
#include "TestTime.h"
#include "core/ColorMath.h"
#include "core/Hotkeys.h"
#include "core/Lang.h"
#include "core/Modes.h"
#include "core/Night.h"
#include "core/Reminders.h"
#include "core/SessionPlan.h"
#include "core/Settings.h"
#include "core/ToneCurve.h"
#include "core/VisibilityGuard.h"
#include <map>

using namespace bb;

// ---- Colour maths ----

TEST(KelvinScale)
{
    CHECK_NEAR(ColorMath::Kelvin(0), 6500, 1e-9);
    CHECK_NEAR(ColorMath::Kelvin(100), 1900, 1e-9);
    CHECK_EQ(ColorMath::KelvinFor(25), 5350);
    ColorMath::Rgb neutral = ColorMath::KelvinToRgb(6500);
    CHECK_NEAR(neutral.r, 1, 1e-6);
    CHECK_NEAR(neutral.g, 1, 1e-6);
    CHECK_NEAR(neutral.b, 1, 1e-6);
    ColorMath::Rgb warm = ColorMath::KelvinToRgb(3000);
    CHECK(warm.b < warm.g && warm.g < warm.r + 1e-6);
}

TEST(ColorMatricesUnchanged)
{
    double sum = 0;
    for (int effect = 0; effect <= 4; effect++)
        for (int style = 0; style <= 2; style++)
            for (int invert = 0; invert <= 1; invert++)
                for (double k : {6500.0, 4000.0, 1900.0}) {
                    ColorMath::Matrix m = ColorMath::Build(ColorMath::KelvinToRgb(k), invert != 0, effect, style);
                    for (size_t i = 0; i < m.size(); i++) sum += m[i] * (i + 1);
                }
    CHECK_GOLDEN(sum, 6247.55512881);
}

TEST(ColorEffectsThroughPixels)
{
    int r = 200, g = 120, b = 40;
    ColorMath::Through(ColorMath::Grayscale, 0, r, g, b);
    CHECK(r == g && g == b);
    long sum = 0;
    for (int effect = 0; effect <= 4; effect++)
        for (int style = 0; style <= 2; style++)
            for (int v = 0; v < 256; v += 17) {
                int rr = v, gg = 255 - v, bb2 = (v * 7) % 256;
                ColorMath::Through(effect, style, rr, gg, bb2);
                sum += rr * 3 + gg * 5 + bb2 * 7;
            }
    CHECK_GOLDEN(static_cast<double>(sum), 386802);
}

// ---- Tone curves ----

TEST(ToneCurveEnds)
{
    for (int level = 0; level < 4; level++) {
        CHECK_NEAR(ToneCurve::Flat(level, 0), 0, 1e-9);
        CHECK(ToneCurve::Flat(level, 1) <= 1 + 1e-9);
        CHECK(ToneCurve::Gamma(level) >= 1);
        CHECK(ToneCurve::White(level) <= 1);
    }
    CHECK_NEAR(ToneCurve::Net(0.5, 0, 0, 0), 0.5, 1e-9);   // all three sliders at zero change nothing
    CHECK_NEAR(ToneCurve::CapFor(0), 1, 1e-9);
}

TEST(ToneCurvesUnchanged)
{
    double net = 0, flat = 0, remap = 0, map = 0, lookup = 0, params = 0;
    for (int i = 0; i <= 20; i++) {
        double x = i / 20.0;
        for (int sh : {0, 30, 100}) for (int sq : {0, 50}) for (int li : {0, 60}) net += ToneCurve::Net(x, sh, sq, li) * (i + 1);
        for (int level = 0; level < 4; level++) {
            flat += ToneCurve::Flat(level, x) * (level + 1);
            remap += ToneCurve::Remap(level, x) * (level + 1);
            double r, g, b;
            ToneCurve::Map(x, x * x, 1 - x, level, r, g, b);
            map += r + 2 * g + 3 * b;
        }
        lookup += ToneCurve::Lookup(x * 0.8, 0.01, [](double v) { return v * v; });
    }
    for (int level = 0; level < 4; level++) {
        ToneCurve::SmartParams p = ToneCurve::ForLevel(level);
        params += p.compress + 2 * p.expand + 3 * p.dimStops + 5 * ToneCurve::Gamma(level) + 7 * ToneCurve::White(level);
    }
    for (int li = 0; li <= 100; li += 10) params += ToneCurve::CapFor(li);
    CHECK_GOLDEN(net, 1402.30698361);
    CHECK_GOLDEN(flat, 67.9347671183);
    CHECK_GOLDEN(remap, -366.449999524);
    CHECK_GOLDEN(map, 169.563412318);
    CHECK_GOLDEN(lookup, 20);
    CHECK_GOLDEN(params, 55.5999995232);
}

// ---- Visibility guard ----

TEST(GuardLeavesAPlainScreenAlone)
{
    auto plain = [](double b, double, double x) { return b * x; };
    VisibilityGuard::Result r = VisibilityGuard::Solve(1.0, plain);
    CHECK_NEAR(r.brightness, 1, 1e-9);
    CHECK_NEAR(r.ease, 1, 1e-9);
}

TEST(GuardUnchanged)
{
    double sum = 0;
    for (double bright : {0.2, 0.35, 0.5, 0.75, 1.0})
        for (int level = 0; level < 4; level++) {
            double gamma = ToneCurve::Gamma(level), white = ToneCurve::White(level);
            auto preset = [=](double b, double e, double x) { return b * white * std::pow(x, 1 + (gamma - 1) * e); };
            auto flat = [=](double b, double, double x) { return b * ToneCurve::Flat(level, x); };
            VisibilityGuard::Result a = VisibilityGuard::Solve(bright, preset), c = VisibilityGuard::Solve(bright, flat);
            sum += a.brightness + 2 * a.ease + 3 * c.brightness + 5 * c.ease;
        }
    CHECK_GOLDEN(sum, 190.42);
}

// ---- The night schedule ----

TEST(NightCustomHours)
{
    Settings s;
    s.ScheduleEnabled = true;
    s.ScheduleMode = 1;
    s.NightFrom = 21 * 60;
    s.NightTo = 7 * 60;
    s.NightFade = 60;
    CHECK_NEAR(Night::Factor(s, TestTime(2026, 10, 26, 12, 0)), 0, 1e-9);   // day
    CHECK_NEAR(Night::Factor(s, TestTime(2026, 10, 26, 23, 0)), 1, 1e-9);   // full night
    double easing = Night::Factor(s, TestTime(2026, 10, 26, 21, 30));
    CHECK(easing > 0 && easing < 1);
    CHECK_NEAR(easing, 0.5, 1e-9);   // half way through the hour, on a symmetric ease
    CHECK_NEAR(Night::Blue(s, 0), s.BlueFilter, 1e-9);
    CHECK_NEAR(Night::Blue(s, 1), s.NightBlue, 1e-9);
}

TEST(NightUnchanged)
{
    Settings s;
    s.ScheduleEnabled = true;
    s.City = L"41.01, 28.98";   // Istanbul, as coordinates (from Windows location)
    double sum = 0;
    for (int month = 1; month <= 12; month += 1)
        for (int hour = 0; hour < 24; hour += 1) sum += Night::Factor(s, TestTime(2026, month, 15, hour, 20)) * (month + hour);
    s.ScheduleMode = 1;
    s.NightFade = 90;
    for (int minute = 0; minute < 1440; minute += 7) sum += Night::Factor(s, TestTime(2026, 3, 1, minute / 60, minute % 60));
    CHECK_GOLDEN(sum, 2435.76523431);
}

TEST(NightStatusText)
{
    Lang::Use(L"en");
    Settings s;
    s.ScheduleEnabled = true;
    s.City = L"41.01, 28.98";
    // Istanbul at midsummer, on a Central European summer clock (UTC+2): sunset 20:39 and sunrise 05:32 in Istanbul's
    // own time are 19:39 and 04:32 here.
    CHECK_EQ(Night::Status(s, TestTime(2026, 6, 21, 12, 0)),
             std::wstring(L"Sunset 19:39, sunrise 04:32 in your location (from Windows).\nResting until 19:39."));
    s.ScheduleMode = 1;
    CHECK_EQ(Night::Status(s, TestTime(2026, 6, 21, 12, 0)), std::wstring(L"On from 21:00 to 07:00.\nResting until 21:00."));
    CHECK_EQ(Night::Hm(21 * 60 + 5), std::wstring(L"21:05"));
    CHECK_EQ(Night::Coordinates(41.0123, 28.9789), std::wstring(L"41.01, 28.98"));
    CHECK(!Night::FindCities(L"ista", 6).empty());
}

// ---- Modes ----

TEST(ModesKeepYourOwnValues)
{
    Settings s;
    s.Brightness = 64;
    s.BlueFilter = 33;
    Modes::Choose(s, Modes::Reading);
    CHECK_EQ(s.Brightness, 85);
    CHECK_EQ(s.BlueFilter, 50);
    Modes::Choose(s, Modes::Custom);
    CHECK_EQ(s.Brightness, 64);   // back to your own
    CHECK_EQ(s.BlueFilter, 33);
    Modes::Choose(s, Modes::Editing);
    CHECK(!s.ToneEnabled);
    Modes::Choose(s, Modes::Pause);
    CHECK(!s.FilterEnabled);
}

TEST(AppRulesRoundTrip)
{
    Settings s;
    Modes::SaveRules(s, {{L"game.exe", Modes::Game, L"C:\\Games\\game.exe"}, {L"code.exe", Modes::Editing, L""}});
    std::vector<Modes::Rule> rules = Modes::Rules(s);
    CHECK_EQ(rules.size(), size_t{2});
    CHECK_EQ(Modes::RuleFor(s, L"game.exe"), static_cast<int>(Modes::Game));   // (the app in front comes lower-cased)
    CHECK_EQ(Modes::RuleFor(s, L"code.exe"), static_cast<int>(Modes::Editing));
    CHECK_EQ(Modes::RuleFor(s, L"other.exe"), -1);
}

// ---- Shortcuts ----

TEST(ShortcutsDefaultsAndChanges)
{
    Settings s;
    CHECK_EQ(Hotkeys::Get(s, Hotkeys::Filter), std::wstring(L"Ctrl+Alt+Shift+F"));
    Hotkeys::Set(s, Hotkeys::Filter, L"Ctrl+Alt+G");
    CHECK_EQ(Hotkeys::Get(s, Hotkeys::Filter), std::wstring(L"Ctrl+Alt+G"));
    Hotkeys::Set(s, Hotkeys::Filter, L"Ctrl+Alt+Shift+F");   // back to the default: nothing kept
    CHECK(s.Hotkeys.empty());
    CHECK_EQ(Hotkeys::Pretty(L"Ctrl+Alt+Up"), std::wstring(L"Ctrl + Alt + \u2191"));
    CHECK(Hotkeys::Repeats(Hotkeys::Brighter));
    CHECK(!Hotkeys::Repeats(Hotkeys::Filter));
}

// ---- Reminders ----

// What some hours of ticks bring: per kind, how many and the second the first one came.
static std::map<int, std::pair<int, int>> RunTicks(Reminders& r, int seconds, int idle = 0, bool fullscreen = false)
{
    std::map<int, std::pair<int, int>> seen;
    for (int t = 1; t <= seconds; t++) {
        Reminders::Due d = r.Tick(idle, fullscreen);
        if (d == Reminders::Due::Nothing) continue;
        auto& [count, first] = seen[static_cast<int>(d)];
        if (count++ == 0) first = t;
        if (d == Reminders::Due::LookAway || d == Reminders::Due::StandUp) r.BreakFinished(d == Reminders::Due::StandUp, 0);
    }
    return seen;
}

TEST(RemindersTiming)
{
    Settings s;
    s.LongBreakEnabled = true;
    Reminders r(s);
    auto seen = RunTicks(r, 3 * 3600);
    CHECK_EQ(seen[static_cast<int>(Reminders::Due::Blink)].second, 15);        // every 15 s
    CHECK_EQ(seen[static_cast<int>(Reminders::Due::LookAway)].second, 1200);   // after 20 min
    CHECK_EQ(seen[static_cast<int>(Reminders::Due::StandUp)].second, 3600);    // after an hour
    CHECK_EQ(seen[static_cast<int>(Reminders::Due::StandUp)].first, 3);    // in 3 hours
    CHECK_EQ(seen[static_cast<int>(Reminders::Due::LookAway)].first, 6);   // 9 due, 3 covered by the stand-up breaks
    CHECK_GOLDEN(seen[static_cast<int>(Reminders::Due::Blink)].first, 711);
    Reminders away(s);
    CHECK(RunTicks(away, 3600, Reminders::AwaySeconds).empty());   // nobody at the PC: nothing to remind
    Reminders full(s);
    CHECK(RunTicks(full, 3600, 0, true).empty());   // a full-screen app holds everything back
}

// While the focus timer runs, the stand-up countdown waits but look-away breaks still come - even when a stand-up
// break was moments away as it started (that one mustn't keep covering them).
TEST(RemindersDuringFocus)
{
    Settings s;
    s.LongBreakEnabled = true;
    s.BlinkEnabled = false;
    Reminders r(s);
    for (int t = 0; t < 3500; t++) r.Tick(0, false);   // the stand-up break 100 s away
    int lookAway = 0, standUp = 0;
    for (int t = 0; t < 3600; t++) {
        Reminders::Due d = r.Tick(0, false, false);
        if (d == Reminders::Due::StandUp) standUp++;
        if (d == Reminders::Due::LookAway) {
            lookAway++;
            r.BreakFinished(false, 0);
        }
    }
    CHECK_EQ(standUp, 0);
    CHECK_EQ(lookAway, 3);   // every 20 min
}

// ---- Guided-break timing ----

TEST(SessionPlanUnchanged)
{
    std::vector<std::wstring> keys{L"far", L"sides", L"updown", L"circle", L"close"};
    std::map<std::wstring, double> spoken{{L"far", 4.7}, {L"sides", 3.3}, {L"updown", 2.2}, {L"circle", 2.9}, {L"close", 3.9}, {L"done", 2.4}};
    for (bool voiced : {false, true}) {
        SessionPlan p(keys, L"done", 40, 4, 1.5, voiced, {1, 1, 1, 1.4, 1}, [&](const std::wstring& k) { return spoken[k]; }, 0.4);
        double sum = p.end * 3 + p.lead * 5;
        for (size_t i = 0; i < p.start.size(); i++) sum += p.start[i] * (i + 1) + p.length[i] * (i + 2);
        double into;
        CHECK_EQ(p.At(p.start[2] + 0.01, into), 2);
        CHECK(p.At(-1, into) < 0);
        CHECK_GOLDEN(sum, voiced ? 576.666666667 : 594.074074074);
    }
}

// ---- Translations ----

TEST(TranslationFormatting)
{
    Lang::Use(L"en");
    CHECK_EQ(Lang::T(L"Paused"), std::wstring(L"Paused"));
    CHECK_EQ(Lang::F(L"Rated {0} of {1}.", {L"3", L"16"}), std::wstring(L"Rated 3 of 16."));
    CHECK_EQ(Lang::F(L"{0:0}% now", {L"42"}), std::wstring(L"42% now"));
}
