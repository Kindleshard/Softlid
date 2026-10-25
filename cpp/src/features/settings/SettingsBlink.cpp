// The Blink page: blink reminders, look-away breaks and stand-up breaks.
#include "features/settings/SettingsWindow.h"
#include "app/App.h"
#include "core/Lang.h"
#include "ui/Theme.h"

namespace bb {
namespace {

using namespace ui;

std::wstring Seconds(int v) { return Lang::F(L"{0} s", {std::to_wstring(v)}); }
std::wstring Minutes(int v) { return Lang::F(L"{0} min", {std::to_wstring(v)}); }

} // namespace

void SettingsWindow::BuildBlinkPage(Stack& st)
{
    Header(st, L'\uE890', L"Blink reminder", L"Relaxed eyes blink ~15 times a minute; staring at a screen, as few as ~5.", s.BlinkEnabled,
           [this](bool v) { s.BlinkEnabled = v; app.Changed(); });
    SliderRow(st, L"Remind me every", 3, 120, s.BlinkInterval, defaults.BlinkInterval, Seconds,
              [this](int v) { s.BlinkInterval = v; app.Changed(); });
    Row& styleRow = st.Add(46);
    AddLabel(styleRow, L"Blink style", Fonts::Body(), Theme::Fg, 24, 10, 120, 26);
    auto& style = styleRow.Add<Segmented>(std::vector<std::wstring>{L"Eye icon", L"Eyelids"});
    style.SetBounds(150, 6, Edge - 150 - 120, 34);
    style.SetSelected(s.BlinkStyle);
    style.tip = Lang::T(L"Eyelids: two dark lids close from the top and bottom of the screen, like your own blink");
    style.onChange = [this](int v) {
        s.BlinkStyle = v;
        app.Changed();
        UpdateRows();
        app.Blink(false);   // show what it looks like
    };
    AddButton(styleRow, L"Preview", Edge - 110, 6, 110, 34).onClick = [this] { app.Blink(false); };
    Shown more = More(st, L"blink");
    Shown icon = And(more, [this] { return s.BlinkStyle == 0; });
    SliderRow(st, L"Icon size", 32, 256, s.IconSize, defaults.IconSize, [](int v) { return std::to_wstring(v) + L" px"; },
              [this](int v) { s.IconSize = v; app.Changed(); app.Blink(true); }, icon);
    SliderRow(st, L"Icon opacity", 10, 100, s.IconOpacity, defaults.IconOpacity, [](int v) { return std::to_wstring(v) + L"%"; },
              [this](int v) { s.IconOpacity = v; app.Changed(); app.Blink(true); }, icon);
    Row& cornerRow = st.Add(72, icon);
    AddLabel(cornerRow, L"Position", Fonts::Body(), Theme::Fg, 24, 22, 80, 26);
    auto& corner = cornerRow.Add<CornerPicker>(s.IconCorner);
    corner.SetBounds(150, 4, 100, 62);
    corner.onChange = [this](int v) { s.IconCorner = v; app.Changed(); app.Blink(true); };
    ToggleRow(st, L"Fade the screen with each blink", s.BlinkFade, [this](bool v) {
        s.BlinkFade = v;
        app.Changed();
        UpdateRows();
        if (v) app.Blink(false);   // show what it looks like
    }, icon);
    Shown darkens = And(more, [this] { return s.BlinkStyle == 1 || s.BlinkFade; });
    SliderRow(st, L"How dark the blink gets", 10, 100, s.BlinkFadeDepth, defaults.BlinkFadeDepth,
              [this](int v) { return s.BlinkStyle == 1 && v >= 100 ? std::wstring(L"Fully closed") : std::to_wstring(v) + L"%"; },
              [this](int v) { s.BlinkFadeDepth = v; app.Changed(); }, darkens);
    Note(st, L"An on-screen shade; the display's backlight is never touched.", 1, darkens);

    Divider(st);
    Header(st, L'\uE909', L"Look-away reminder", L"The 20-20-20 rule: every 20 min, look 20 ft (6 m) away for 20 s.", s.BreakEnabled,
           [this](bool v) { s.BreakEnabled = v; app.Changed(); });
    SliderRow(st, L"Every", 5, 60, s.BreakInterval, defaults.BreakInterval, Minutes, [this](int v) { s.BreakInterval = v; app.Changed(); });
    // (The spoken exercises set a floor on the break's length; that check comes with the voice, in chunk 7.)
    SliderRow(st, L"Look away for", 10, 120, s.BreakDuration, defaults.BreakDuration, Seconds,
              [this](int v) { s.BreakDuration = v; app.Changed(); });
    ToggleRow(st, L"Guided eye exercises during the break", s.Exercises, [this](bool v) { s.Exercises = v; app.Changed(); UpdateRows(); });
    Row& voiceRow = st.Add(34, [this] { return s.Exercises; });
    AddLabel(voiceRow, L"Voice", Fonts::Body(), Theme::Fg, 48, 4, 56, 26);
    AddLabel(voiceRow, L"reads each exercise aloud", Fonts::Small(), Theme::Dim, 108, 8, 260, 20);
    AddToggle(voiceRow, Edge - 44, 6, s.ExerciseVoice, [this](bool v) { s.ExerciseVoice = v; app.Changed(); });
    Row& breakRow = st.Add(46);
    breakStatus = &AddLabel(breakRow, L"", Fonts::Body(), Theme::Dim, 24, 10, Edge - 176 - 32, 26);
    AddButton(breakRow, L"Take a break now", Edge - 176, 6, 176, 34).onClick = [this] { app.StartBreak(false); };
    Shown breakMore = More(st, L"break");
    ToggleRow(st, L"Dark background behind the break", s.BreakBackdrop, [this](bool v) { s.BreakBackdrop = v; app.Changed(); }, breakMore);
    Note(st, L"Off: the eye exercises move over your screen instead of a black background.", 1, breakMore);
    ToggleRow(st, L"Chime when a break starts and ends", s.Sounds, [this](bool v) { s.Sounds = v; app.Changed(); }, breakMore);
    ToggleRow(st, L"Force breaks (no Skip, Snooze or Esc)", s.ForceRest, [this](bool v) { s.ForceRest = v; app.Changed(); }, breakMore);
    Note(st, L"Being away from the PC for 5+ minutes counts as a break.", 1, breakMore);

    Divider(st);
    Header(st, L'\uE805', L"Stand-up reminder", L"Every hour or so, get up, move and stretch for a few minutes.", s.LongBreakEnabled,
           [this](bool v) { s.LongBreakEnabled = v; app.Changed(); });
    SliderRow(st, L"Every", 30, 120, s.LongBreakInterval, defaults.LongBreakInterval, Minutes,
              [this](int v) { s.LongBreakInterval = v; app.Changed(); });
    SliderRow(st, L"Break length", 1, 15, s.LongBreakDuration, defaults.LongBreakDuration, Minutes,
              [this](int v) { s.LongBreakDuration = v; app.Changed(); });
    Row& longRow = st.Add(46);
    longStatus = &AddLabel(longRow, L"", Fonts::Body(), Theme::Dim, 24, 10, Edge - 176 - 32, 26);
    AddButton(longRow, L"Stand up now", Edge - 176, 6, 176, 34).onClick = [this] { app.StartBreak(true); };
    Shown standMore = More(st, L"stand");
    ToggleRow(st, L"Guided stretches with an animated coach", s.StretchGuided,
              [this](bool v) { s.StretchGuided = v; app.Changed(); UpdateRows(); }, standMore);
    Row& stretchVoiceRow = st.Add(34, [this, standMore] { return standMore() && s.StretchGuided; });
    AddLabel(stretchVoiceRow, L"Voice", Fonts::Body(), Theme::Fg, 48, 4, 56, 26);
    AddLabel(stretchVoiceRow, L"announces each stretch aloud", Fonts::Small(), Theme::Dim, 108, 8, 260, 20);
    AddToggle(stretchVoiceRow, Edge - 44, 6, s.StretchVoice, [this](bool v) { s.StretchVoice = v; app.Changed(); });
    Note(st, L"Being away from the PC for the break length counts as one. A look-away break due in the last 2 minutes before it is skipped.",
         2, standMore);
    st.Add(16);
}

} // namespace bb
