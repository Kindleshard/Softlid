// The Options page (the cog): BlinkBuddy itself, mini mode and updates.
#include "features/settings/SettingsWindow.h"
#include "app/App.h"
#include "core/Lang.h"
#include "core/Version.h"
#include "ui/Theme.h"

namespace bb {
namespace {

using namespace ui;

std::wstring Ms(int v) { return v >= 1000 ? L"1 s" : std::to_wstring(v) + L" ms"; }

} // namespace

void SettingsWindow::BuildOptionsPage(Stack& st)
{
    Row& head = st.Add(64);
    Title(head, L'\uE713', L"BlinkBuddy options");
    AddLabel(head, L"How BlinkBuddy itself behaves.", Fonts::Small(), Theme::Dim, 56, 38, Edge - 56, 20);
    // The language: Windows' own, or one of the list (each named in its own words). The window is built again in it.
    Row& langRow = st.Add(46);
    AddLabel(langRow, L"Language", Fonts::Body(), Theme::Fg, 24, 10, 120, 26);
    std::vector<std::wstring> names{Lang::T(L"Same as Windows")};
    int chosen = 0;
    for (const auto& info : Lang::All()) {
        if (s.Language == info.code) chosen = static_cast<int>(names.size());
        names.push_back(info.name);
    }
    auto& language = langRow.Add<DropDown>(std::vector<std::wstring>{});
    language.SetPlainItems(names);
    language.SetBounds(150, 6, Edge - 150, 34);
    language.SetSelected(chosen);
    language.onChange = [this](int i) { app.SetLanguage(i == 0 ? L"" : Lang::All()[i - 1].code); };

    ToggleRow(st, L"Start with Windows", app.RunAtStartup(), [this](bool v) { app.SetRunAtStartup(v); });
    ToggleRow(st, L"Pause reminders in full-screen apps", s.PauseInFullscreen, [this](bool v) { s.PauseInFullscreen = v; app.Changed(); });

    Row& viewRow = st.Add(50);
    AddLabel(viewRow, L"View", Fonts::Body(), Theme::Fg, 24, 12, 100, 26);
    auto& view = viewRow.Add<Segmented>(std::vector<std::wstring>{L"Simple", L"Advanced"});
    view.SetBounds(150, 8, Edge - 150, 34);
    view.SetSelected(s.AdvancedView ? 1 : 0);
    view.onChange = [this](int v) {
        if (v == 1) {
            s.AdvancedView = true;
            app.Changed();
        } else {
            app.UseCompactView();   // switches the advanced features off
        }
        app.Rebuild(true);   // built again for the new view, every control showing the settings as they now are
    };
    Note(st, L"Simple shows just the essentials, without \"More options\". It turns off Invert colors, Film grain, cooler than "
             L"daylight, custom night hours, forced breaks and keeping the PC awake; tone uses the Standard "
             L"engine at Gentle to Strong. Modes for apps are kept but pause.", 4);

    // The accent colour: Windows' own, or one picked here (its swatch opens the colour wheel). The window is built
    // again in the new colour once the wheel closes.
    Row& accentRow = st.Add(46);
    AddLabel(accentRow, L"Accent color", Fonts::Body(), Theme::Fg, 24, 10, 120, 26);
    auto& accentFrom = accentRow.Add<Segmented>(std::vector<std::wstring>{L"Match Windows", L"Custom"});
    accentFrom.SetBounds(150, 6, Edge - 150 - (s.AccentFromWindows ? 0 : 46), 34);
    accentFrom.SetSelected(s.AccentFromWindows ? 0 : 1);
    accentFrom.onChange = [this](int v) { app.SetAccent(v == 0, s.AccentColor); };
    if (!s.AccentFromWindows) {
        auto& accent = accentRow.Add<ColorButton>();
        accent.SetBounds(Edge - 36, 6, 34, 34);
        accent.SetValue(Theme::Accent());
        accent.tip = Lang::T(L"Choose a color");
        accent.onChosen = [this, &accent] {
            if (accent.Value() != Theme::Accent()) app.SetAccent(false, accent.Value());
        };
    }

    // Closing this window: ask each time, keep BlinkBuddy running in the tray, or exit.
    Row& closeRow = st.Add(80);
    AddLabel(closeRow, L"When this window is closed", Fonts::Body(), Theme::Fg, 24, 6, Edge - 24, 26);
    auto& close = closeRow.Add<DropDown>(std::vector<std::wstring>{L"Ask me", L"Keep running in the tray", L"Exit BlinkBuddy"});
    close.SetBounds(24, 38, Edge - 24, 34);
    close.SetSelected(s.CloseAction);
    close.onChange = [this](int v) { s.CloseAction = v; app.Changed(); };

    // (Keyboard shortcuts, each changeable in a window of its own, come with chunk 6.)

    // Keeping the PC awake (the tray menu has it too, for an hour or two).
    Shown advanced = [this] { return s.AdvancedView; };
    awakeToggle = &ToggleRow(st, L"Keep PC awake", app.KeepingAwake(), [this](bool v) { app.SetKeepAwake(v); }, advanced);
    Note(st, L"Windows won't go to sleep or turn the screen off. The tray menu can also keep it awake for 1 or 2 hours.", 2, advanced);

    BuildMiniOptions(st);
    BuildUpdates(st);

    Divider(st);
    Row& resetRow = st.Add(46);
    AddLabel(resetRow, L"Every setting back to how it started", Fonts::Body(), Theme::Fg, 24, 10, Edge - 176 - 32, 26);
    AddButton(resetRow, L"Reset all settings", Edge - 176, 6, 176, 34).onClick = [this] { app.ResetAll(); };
    st.Add(24);   // room below the last row
}

// Mini mode: on or off, how it fades, and the glow that now and then breathes round it. (Its icons, drawn as mini mode
// draws them, are chosen here once mini mode itself arrives, in chunk 6.)
void SettingsWindow::BuildMiniOptions(Stack& st)
{
    Divider(st);
    Shown on = [this] { return s.MiniEnabled; };
    Header(st, L'\uE944', L"Mini mode", L"A small icon that stays on top of other windows.", s.MiniEnabled, [this](bool v) {
        app.SetMiniEnabled(v);
        UpdateRows();
    });
    Note(st, L"Switch to it with the button next to the title bar, or by minimising this window. Hover over it for quick "
             L"controls; click it to come back here.", 2, on);
    ToggleRow(st, L"Show the time to the next break on the icon's rim", s.MiniCountdown,
              [this](bool v) { s.MiniCountdown = v; app.Changed(); }, on);
    Shown more = More(st, L"mini", on), glow = And(more, [this] { return s.MiniOrbit; });
    SliderRow(st, L"Fade-out delay after hovering", 10, 1000, s.MiniFadeDelay, 500, Ms,
              [this](int v) { s.MiniFadeDelay = v; app.Changed(); }, more);
    SliderRow(st, L"Fade-out speed", 0, 200, s.MiniFadeTime, 150, [](int v) { return v == 0 ? std::wstring(L"Instant") : Ms(v); },
              [this](int v) { s.MiniFadeTime = v; app.Changed(); }, more);
    ToggleRow(st, L"The icon breathes with a soft glow now and then", s.MiniOrbit,
              [this](bool v) { s.MiniOrbit = v; app.Changed(); UpdateRows(); }, more);
    SliderRow(st, L"Every", 0, 200, s.MiniOrbitSeconds, 60,
              [](int v) { return v == 0 ? Lang::T(L"Continuous") : Lang::F(L"{0} s", {std::to_wstring(v)}); },
              [this](int v) { s.MiniOrbitSeconds = v; app.Changed(); }, glow);
    Row& colorRow = st.Add(46, glow);
    AddLabel(colorRow, L"Color", Fonts::Body(), Theme::Fg, 24, 10, 200, 26);
    auto& color = colorRow.Add<ColorButton>();
    color.SetBounds(Edge - 36, 6, 34, 34);
    color.allowAccent = true;
    color.SetUseAccent(s.MiniOrbitColor == 0);
    color.SetValue(s.MiniOrbitColor == 0 ? Theme::Accent() : s.MiniOrbitColor | 0xFF000000);
    color.presets = {Theme::Accent()};
    color.presets.insert(color.presets.end(), std::begin(Palette::Glows) + 1, std::end(Palette::Glows));
    color.tip = Lang::T(L"Choose a color");
    color.onChange = [this, &color] { s.MiniOrbitColor = color.UseAccent() ? 0 : color.Value(); app.Changed(); };
}

// Updates: this version and how things stand. (Checking, downloading and installing come with the release, chunk 9.)
void SettingsWindow::BuildUpdates(Stack& st)
{
    Divider(st);
    Row& row = st.Add(46);
    AddLabel(row, L"", Fonts::Body(), Theme::Fg, 24, 10, 220, 26).SetPlainText(Lang::F(L"Version {0}", {Version}));
    Row& statusRow = st.Add(28);
    AddLabel(statusRow, L"Updates aren't available yet: this copy of BlinkBuddy has no release channel.", Fonts::Small(), Theme::Dim,
             24, 2, Edge - 24, 22);
    ToggleRow(st, L"Check for updates automatically (weekly)", s.UpdateCheck, [this](bool v) { s.UpdateCheck = v; app.Changed(); });
}

} // namespace bb
