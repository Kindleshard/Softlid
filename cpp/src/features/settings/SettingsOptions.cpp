// The Options page (the cog): BlinkBuddy itself, mini mode and updates.
#include "features/settings/SettingsWindow.h"
#include "features/settings/SettingsHost.h"
#include "core/Lang.h"
#include "core/Version.h"
#include "features/settings/ShortcutsDialog.h"
#include "features/update/Updater.h"
#include "ui/MiniArt.h"
#include "ui/Theme.h"
#include <shellapi.h>

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

    // Keyboard shortcuts: each one changeable, in a window of its own.
    Row& keysRow = st.Add(46, [this] { return s.AdvancedView; });
    AddLabel(keysRow, L"Keyboard shortcuts", Fonts::Body(), Theme::Fg, 24, 10, 240, 26);
    AddButton(keysRow, L"Change...", Edge - 176, 6, 176, 34).onClick = [this] { ShortcutsDialog::Open(app, Handle(), !unfoldAll); };

    // Keeping the PC awake (the tray menu has it too, for an hour or two).
    Shown advanced = [this] { return s.AdvancedView; };
    awakeToggle = &ToggleRow(st, L"Keep PC awake", app.KeepingAwake(), [this](bool v) { app.SetKeepAwake(v); }, advanced);
    Note(st, L"Windows won't go to sleep or turn the screen off. The tray menu can also keep it awake for 1 or 2 hours.", 2, advanced);

    BuildMiniOptions(st);
    BuildUpdates(st);
    BuildResourceUse(st);

    Divider(st);
    Row& resetRow = st.Add(46);
    AddLabel(resetRow, L"Every setting back to how it started", Fonts::Body(), Theme::Fg, 24, 10, Edge - 176 - 32, 26);
    AddButton(resetRow, L"Reset all settings", Edge - 176, 6, 176, 34).onClick = [this] { app.ResetAll(); };
    st.Add(24);   // room below the last row
}

// Mini mode: on or off (the button by the title bar switches to it), which icon, how it fades, and the glow that now
// and then breathes round it.
void SettingsWindow::BuildMiniOptions(Stack& st)
{
    Divider(st);
    Shown on = [this] { return s.MiniEnabled; };
    Header(st, L'\uE944', L"Mini mode", L"A small icon that stays on top of other windows.", s.MiniEnabled, [this](bool v) {
        app.SetMiniEnabled(v);
        titleMini->SetVisible(v);
        UpdateRows();
    });
    Note(st, L"Switch to it with the button next to the title bar, or by minimising this window. Hover over it for quick "
             L"controls; click it to come back here.", 2, on);
    // The icons as previews (each named in its tip), drawn with the screen's current brightness and warmth.
    Row& iconRow = st.Add(64, on);
    AddLabel(iconRow, L"Icon", Fonts::Body(), Theme::Fg, 24, 18, 120, 26);
    auto& icon = iconRow.Add<TilePicker>(4, s.MiniIcon);
    icon.SetBounds(150, 6, 4 * 48 + 3 * 13 + 2, 48);
    icon.painter = [this](Canvas& c, const D2D1_RECT_F& r, int i, float awake) {
        // On its dark disc, as mini mode draws it.
        float w = Width(r), d = w * 0.84f, cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
        c.FillCircle(cx, cy, d / 2, Palette::Argb(34, 36, 42));
        float box = d * 0.66f, bright = s.FilterEnabled ? (s.Brightness - 20) / 80.0f : 1.0f;
        MiniArt::DrawIcon(c.Target(), {cx - box / 2, cy - box / 2, cx + box / 2, cy + box / 2}, i, awake, bright, app.WarmthNow());
    };
    icon.onChange = [this](int v) { s.MiniIcon = v; app.Changed(); };
    icon.onHover = [&icon](int i) {
        static const wchar_t* const names[] = {L"Eye", L"Lamp", L"Sun", L"Laptop"};
        icon.tip = i >= 0 ? Lang::T(names[i]) : L"";
    };
    Note(st, L"Eye: opens when you come near. Lamp: as bright as the screen. Sun: glows when you come near. Laptop: shows "
             L"the screen's brightness and the blue light filter.", 2, on);
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

// Updates: this version and, under it, how things stand: green with a thumbs-up when up to date; orange when a newer
// version is out, and the button turns into an orange "Update" that downloads it (with a progress bar), puts it in
// place and restarts - nothing to set up. A quiet check runs when this page is built.
void SettingsWindow::BuildUpdates(Stack& st)
{
    Divider(st);
    Row& row = st.Add(46);
    AddLabel(row, L"", Fonts::Body(), Theme::Fg, 24, 10, 220, 26).SetPlainText(Lang::F(L"Version {0}", {Version}));
    updateButton = &AddButton(row, L"Check for updates", Edge - 176, 6, 176, 34);
    updateButton->onClick = [this] {
        if (app.Update().release) app.InstallUpdate();
        else app.CheckForUpdates();
        RefreshUpdates();
        UpdateRows();
    };
    updateButton->SetVisible(Updater::Available());
    Row& statusRow = st.Add(28);
    updateMark = &statusRow.Add<Icon>(L'\uE8E1', 11.0f, Theme::Good);   // thumbs up
    updateMark->SetBounds(22, 2, 22, 22);
    updateStatus = &AddLabel(statusRow, L"", Fonts::Small(), Theme::Dim, 46, 2, Edge - 46, 22);
    Row& progressRow = st.Add(18, [this] { return app.Update().downloading; });
    updateProgress = &progressRow.Add<ProgressLine>();
    updateProgress->SetBounds(24, 5, Edge - 24, 6);
    ToggleRow(st, L"Check for updates automatically (weekly)", s.UpdateCheck, [this](bool v) { s.UpdateCheck = v; app.Changed(); });
    if (Updater::Available() && app.Update().text.empty()) app.CheckForUpdates();
    shownDownloading = app.Update().downloading;   // (the rows are placed once the page is built)
    RefreshUpdates();

    // Supporting BlinkBuddy (only once there is somewhere to send people).
    if (DonateUrl[0]) {
        Divider(st);
        Row& support = st.Add(46);
        AddLabel(support, L"Enjoying BlinkBuddy? It's free; a tip keeps it going.", Fonts::Body(), Theme::Fg, 24, 10, Edge - 140 - 32, 26);
        AddButton(support, L"Support...", Edge - 140, 6, 140, 34).onClick = [] {
            ShellExecuteW(nullptr, L"open", DonateUrl, nullptr, nullptr, SW_SHOWNORMAL);
        };
    }
}

// The update line following the app's state (each second, and whenever the updater reports).
void SettingsWindow::RefreshUpdates()
{
    using Look = Updater::State::Look;
    const Updater::State& u = app.Update();
    std::wstring text = !Updater::Available() ? Lang::T(L"Updates aren't available yet: this copy of BlinkBuddy has no release channel.")
                        : u.text.empty()      ? Lang::T(L"Not checked yet.")
                                              : u.text;
    updateStatus->SetPlainText(text);
    updateStatus->SetColor(u.look == Look::UpToDate ? Theme::Good : u.look == Look::Newer ? Theme::Warn : Theme::Dim);
    bool good = u.look == Look::UpToDate;
    updateMark->SetVisible(good);
    float x = good ? 46.0f : 24.0f;
    updateStatus->SetBounds(x, 2, Edge - x, 22);
    updateButton->SetText(u.release ? L"Update" : L"Check for updates");
    updateButton->SetTint(u.release ? Theme::Warn : 0);
    updateButton->SetEnabled(!u.busy);
    updateProgress->SetProgress(u.progress);
    if (u.downloading != shownDownloading) {
        shownDownloading = u.downloading;
        UpdateRows();
    }
}

} // namespace bb

namespace bb {

using namespace ui;

namespace {

std::wstring OneDecimal(double v)
{
    wchar_t text[32];
    swprintf_s(text, L"%.1f", v);
    return text;
}

} // namespace

// What BlinkBuddy itself uses right now (Advanced view): the processor, the graphics card and memory, with the last
// minute of the first two as a graph. Measured only while this page shows - so drawing this window is part of it.
void SettingsWindow::BuildResourceUse(Stack& st)
{
    Shown advanced = [this] { return s.AdvancedView; };
    Divider(st, advanced);
    SectionHead(st, L'\uE9D9', L"Resource use", L"What BlinkBuddy itself uses right now, as Task Manager counts it.", advanced);
    Row& graphRow = st.Add(84, advanced);
    usageGraph = &graphRow.Add<UsageGraph>();
    usageGraph->SetBounds(24, 4, Edge - 24, 72);
    usageGraph->tip = Lang::T(L"The last minute: processor (accent color) and graphics card (green)");
    Row& textRow = st.Add(88, advanced);
    usageText = &AddLabel(textRow, L"", Fonts::Small(), Theme::Dim, 24, 2, Edge - 24, 84);
    usageText->wrap = usageText->top = true;
    Note(st, L"Measured only while this page shows, so drawing this window is part of what you see here. Closed, "
             L"BlinkBuddy mostly waits and uses next to nothing.", 2, advanced);
    // A diagnostic copy shows dashes, so its screenshots stay the same (unless "--measure" asks it to measure).
    measuring = !unfoldAll || std::wcsstr(GetCommandLineW(), L"--measure");
    RefreshUsage();
}

// Once a second while the Options page shows.
void SettingsWindow::RefreshUsage()
{
    if (!usageText) return;
    if (measuring && (!s.AdvancedView || tabs->Selected() != 4)) return;
    ResourceUse u{0, 0, 0, 0, 0, 0};
    if (measuring) {
        if (!meter) meter = std::make_unique<ResourceMeter>();   // (made the first time it shows)
        u = meter->Sample();
        usageGraph->Add(u.cpu, std::max(0.0, u.gpu));
    }
    auto num = [this](double v) { return measuring ? OneDecimal(v) : std::wstring(L"\u2013"); };
    std::wstring text = Lang::F(L"Processor: {0}% of the whole CPU", {num(u.cpu)}) + L"\n";
    text += u.gpu < 0 ? Lang::T(L"Graphics card: Windows doesn't report it here")
                      : Lang::F(L"Graphics card: {0}% (its busiest engine)", {num(u.gpu)});
    // Memory as Task Manager's Memory column counts it (its own pages in RAM), then all it has set aside.
    text += L"\n" + (u.ramOwnMb < 0 ? Lang::F(L"Memory: {0} MB in RAM", {num(u.ramMb)})
                                     : Lang::F(L"Memory: {0} MB in RAM, plus {1} MB of Windows libraries it shares",
                                               {num(u.ramOwnMb), num(u.ramSharedMb)}));
    text += L"\n" + Lang::F(L"Set aside in all (in RAM or the page file): {0} MB", {num(u.privateMb)});
    usageText->SetPlainText(text);
}

} // namespace bb
