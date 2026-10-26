// The Schedule page: the night schedule and its place (synced by itself, or a city you pick), and Windows Night light.
#include "features/settings/SettingsWindow.h"
#include "features/settings/SettingsHost.h"
#include "core/ColorMath.h"
#include "core/Lang.h"
#include "core/Night.h"
#include "ui/Theme.h"
#include <shellapi.h>
#include <algorithm>

namespace bb {

using namespace ui;

// The cities matching what's typed, listed under the box (dark, like the rest): the one under the pointer or picked
// with the arrow keys is lit; a click chooses it.
class SettingsWindow::CityList : public Control {
public:
    static constexpr float ItemHeight = 26;
    std::vector<std::wstring> items;
    int selected = 0;
    std::function<void(const std::wstring&)> onChoose;

    CityList() { cursor = Cursor::Hand; }

    void Paint(Canvas& c) override
    {
        c.Fill({0, 0, W(), H()}, Palette::Argb(45, 45, 48));
        for (size_t i = 0; i < items.size(); i++) {
            D2D1_RECT_F r = Rect(0, i * ItemHeight, W(), ItemHeight);
            if (static_cast<int>(i) == selected) c.Fill(r, Palette::Argb(38, 79, 112));
            c.Text(items[i], Fonts::Body(), Inset(r, 10, 0), Theme::Fg);
        }
    }

    void MouseMove(D2D1_POINT_2F p, bool) override
    {
        int i = static_cast<int>(p.y / ItemHeight);
        if (i >= 0 && i < static_cast<int>(items.size()) && i != selected) {
            selected = i;
            Invalidate();
        }
    }

    void Click() override
    {
        if (selected >= 0 && selected < static_cast<int>(items.size()) && onChoose) onChoose(items[selected]);
    }

    void Move(int by)
    {
        if (items.empty()) return;
        selected = std::clamp(selected + by, 0, static_cast<int>(items.size()) - 1);
        Invalidate();
    }
};

void SettingsWindow::BuildSchedulePage(Stack& st)
{
    Toggle& schedule = Header(st, L'\uE708', L"Night schedule", L"Eases the blue light filter in at night and back out in the morning.",
                              s.ScheduleEnabled, [this](bool v) { s.ScheduleEnabled = v; NightMarker(); NightChanged(); UpdateRows(); });
    Row& nightPausedRow = st.Add(26, [this] { return app.Scheduled().ScheduleEnabled && BluePaused(); });
    Label& nightPaused = AddLabel(nightPausedRow, L"", Fonts::Small(), Theme::Warn, 24, 0, Edge - 24, 22);
    updaters.push_back([this, &nightPaused] { nightPaused.SetPlainText(BluePausedText()); });
    BuildNightLight(st);
    // While Windows Night light's schedule is followed, the schedule's own settings step aside (kept for when it isn't).
    Shown own = [this] { return !app.FollowsNightLight(); };
    updaters.push_back([this, &schedule] {
        schedule.SetOn(s.ScheduleEnabled);
        schedule.SetVisible(!app.FollowsNightLight());   // (the sync switch below says what runs the night)
    });
    // Sunset to sunrise, or hours of your own (the Advanced view only: Simple keeps to the sun).
    Shown advanced = And(own, [this] { return s.AdvancedView; }), hours = And(advanced, [this] { return s.ScheduleMode == 1; });
    Row& modeRow = st.Add(46, advanced);
    auto& mode = modeRow.Add<Segmented>(std::vector<std::wstring>{L"Sunset to sunrise", L"Custom hours"});
    mode.SetBounds(24, 4, Edge - 24, 34);
    mode.SetSelected(s.ScheduleMode);
    // The schedule's own strength: at night it takes over from the Blue light filter on the Screen tab.
    Slider& night = SliderRow(st, L"Blue light filter at night", 0, 100, s.NightBlue, defaults.NightBlue,
        [](int v) { return v == 0 ? std::wstring(L"Off") : std::to_wstring(ColorMath::KelvinFor(v)) + L" K"; },
        [this](int v) { s.NightBlue = v; app.PreviewNightStrength(); NightMarker(); NightChanged(); }, own);
    night.warm = true;
    Note(st, L"Sunset and sunrise come from your location in Windows when it's allowed, and from your time zone otherwise.", 2,
         And(advanced, [this] { return s.ScheduleMode == 0; }));
    SliderRow(st, L"Turn on at", 0, 95, s.NightFrom / 15, defaults.NightFrom / 15, [](int v) { return Night::Hm(v * 15); },
              [this](int v) { s.NightFrom = v * 15; NightChanged(); }, hours);
    SliderRow(st, L"Turn off at", 0, 95, s.NightTo / 15, defaults.NightTo / 15, [](int v) { return Night::Hm(v * 15); },
              [this](int v) { s.NightTo = v * 15; NightChanged(); }, hours);
    SliderRow(st, L"Ease in and out over", 0, 36, s.NightFade / 5, defaults.NightFade / 5,
        [](int v) {
            int m = v * 5;
            if (v == 0) return Lang::T(L"No easing");
            if (m < 60) return Lang::F(L"{0} min", {std::to_wstring(m)});
            wchar_t mm[4];
            swprintf_s(mm, L"%02d", m % 60);
            return Lang::F(L"{0} h {1:00} min", {std::to_wstring(m / 60), mm});
        },
        [this](int v) { s.NightFade = v * 5; NightChanged(); }, advanced);
    Note(st, L"It starts easing in at the start time and is fully back to normal at the end time.", 1, advanced);
    Row& statusRow = st.Add(70);
    nightStatus = &AddLabel(statusRow, L"", Fonts::Body(), Theme::Dim, 24, 4, Edge - 24, 62);
    nightStatus->wrap = nightStatus->top = true;
    // When (and with what) the sun times were last synced, in green: Windows location, or the time zone's city.
    Row& syncRow = st.Add(28, [this] { return s.ScheduleEnabled && s.ScheduleMode == 0 && !app.FollowsNightLight(); });
    syncTick = &syncRow.Add<Icon>(L'\uE73E', 10.0f, Theme::Good);   // a check mark
    syncTick->SetBounds(22, 2, 22, 22);
    syncStatus = &AddLabel(syncRow, L"Syncing with your location...", Fonts::Small(), Theme::Dim, 46, 2, Edge - 46, 22);
    BuildCityPicker(st);
    mode.onChange = [this](int v) {
        s.ScheduleMode = v;
        UpdateRows();
        NightChanged();
    };
    updaters.push_back([this, &mode] { mode.SetSelected(s.ScheduleMode); });   // (the Simple view puts it back to the sun)
    st.Add(16);
}

// The line under the status: green with a check when synced (or when you chose the city), red with a mark when it
// couldn't sync, grey while syncing. A change in that state shows or hides the city picker.
void SettingsWindow::ShowSync()
{
    std::wstring text;
    SettingsHost::Sync state = app.SyncState(text);
    syncStatus->SetPlainText(text);
    syncStatus->SetColor(state == SettingsHost::Sync::Failed ? Theme::Bad : state == SettingsHost::Sync::Syncing ? Theme::Dim : Theme::Good);
    syncTick->Set(state == SettingsHost::Sync::Failed ? L'\uE783' : L'\uE73E', state == SettingsHost::Sync::Failed ? Theme::Bad : Theme::Good);
    syncTick->SetVisible(state != SettingsHost::Sync::Syncing);
    if (static_cast<int>(state) == shownSync) return;
    shownSync = static_cast<int>(state);
    UpdateRows();
}

// When syncing fails: a link to pick the city by hand; then a box to type it in, with matching cities listed under it,
// and "Automatic" to go back to syncing by itself.
void SettingsWindow::BuildCityPicker(Stack& st)
{
    Shown sunset = [this] { return s.ScheduleEnabled && s.ScheduleMode == 0 && !app.FollowsNightLight(); };
    Row& linkRow = st.Add(26, [this, sunset] {
        return sunset() && shownSync == static_cast<int>(SettingsHost::Sync::Failed) && !pickerOpen && !s.CityManual;
    });
    auto& link = linkRow.Add<Label>(L"Select your city manually", Fonts::Small(), Theme::Accent());
    link.SetBounds(46, 0, Edge - 46, 22);
    auto& linkArea = linkRow.Add<HotArea>();
    linkArea.SetBounds(46, 0, TextWidth(link.Text(), Fonts::Small()) + 4, 22);
    linkArea.onClick = [this] {
        pickerOpen = true;
        UpdateRows();
        cityBox->StartTyping();
    };

    Row& pickRow = st.Add(46, [this, sunset] { return sunset() && (pickerOpen || s.CityManual); });
    cityBox = &pickRow.Add<TextBox>(60);
    cityBox->SetBounds(24, 6, Edge - 24 - 132, 30);
    cityBox->cue = Lang::T(L"Type your city");
    cityBox->SetText(s.CityManual ? s.City : L"");
    auto& automatic = AddButton(pickRow, L"Automatic", Edge - 120, 4, 120, 32);
    automatic.onClick = [this] {
        pickerOpen = false;
        cityBox->SetText(L"");
        ShowSuggestions({});
        app.SetCity(L"");
        RefreshStatus();
        UpdateRows();
    };

    suggestRow = &st.Add(40, [this, sunset] { return sunset() && (pickerOpen || s.CityManual) && !cityList->items.empty(); });
    cityList = &suggestRow->Add<CityList>();
    cityList->SetBounds(24, 0, Edge - 24 - 132, 34);
    cityList->onChoose = [this](const std::wstring& name) { Later([this, name] { ChooseCity(name); }); };

    cityBox->onChange = [this](const std::wstring& text) { ShowSuggestions(Night::FindCities(text, 6)); };
    cityBox->onKey = [this](UINT key) {
        size_t n = cityList->items.size();
        if (key == VK_DOWN) cityList->Move(1);
        else if (key == VK_UP) cityList->Move(-1);
        else if (key == VK_RETURN && n > 0) ChooseCity(cityList->items[std::max(0, cityList->selected)]);
        else if (key == VK_ESCAPE && n > 0) ShowSuggestions({});
    };
}

// The list under the box, sized to its matches (or gone when there are none).
void SettingsWindow::ShowSuggestions(std::vector<std::wstring> names)
{
    cityList->items = std::move(names);
    cityList->selected = 0;
    float h = cityList->items.size() * CityList::ItemHeight + 2;
    cityList->SetBounds(24, 0, Edge - 24 - 132, h);
    suggestRow->SetBounds(0, suggestRow->Bounds().top, suggestRow->W(), h + 6);
    UpdateRows();
}

void SettingsWindow::ChooseCity(const std::wstring& name)
{
    cityBox->SetText(name);
    ShowSuggestions({});
    app.SetCity(name);
    RefreshStatus();
    UpdateRows();
}

// The green mark on the Screen tab's blue light slider: where the night schedule takes the filter.
// Windows' own Night light, when it is set up here: a switch to sync with it, and what it is doing. Synced, its
// schedule (if it has one) runs the night, and while it is on BlinkBuddy's blue light filter rests. Read-only:
// Night light itself is changed in Windows Settings.
void SettingsWindow::BuildNightLight(Stack& st)
{
    Shown found = [this] { return app.NightLightState().found; };
    ToggleRow(st, L"Sync with Windows Night light", s.NightLightSync, [this](bool v) {
        s.NightLightSync = v;
        NightMarker();
        NightChanged();
        UpdateRows();
    }, found);
    Row& row = st.Add(66, And(found, [this] { return s.NightLightSync; }));
    nightLightStatus = &AddLabel(row, L"", Fonts::Small(), Theme::Dim, 24, 2, Edge - 24 - 150, 62);
    nightLightStatus->wrap = nightLightStatus->top = true;
    AddButton(row, L"Night light...", Edge - 140, 6, 140, 30).onClick = [] {
        ShellExecuteW(nullptr, L"open", L"ms-settings:nightlight", nullptr, nullptr, SW_SHOWNORMAL);
    };
    RefreshNightLight();
}

void SettingsWindow::RefreshNightLight()
{
    const NightLight::State& n = app.NightLightState();
    if (!n.found) return;
    std::wstring now = !n.on ? Lang::T(L"Night light is off now.")
                       : n.byHand ? Lang::T(L"Night light is on now (turned on by hand).")
                                  : Lang::T(L"Night light is on now.");
    std::wstring strength = std::to_wstring(n.Strength());
    std::wstring plan = !n.scheduled ? Lang::T(L"It has no schedule, so BlinkBuddy's own schedule runs.")
                        : n.setHours ? Lang::F(L"BlinkBuddy follows its hours, {0} to {1}, at strength {2}.",
                                               {Night::Hm(n.Start()), Night::Hm(n.End()), strength})
                                     : Lang::F(L"BlinkBuddy follows it from sunset to sunrise ({0} to {1}), at strength {2}.",
                                               {Night::Hm(n.Start()), Night::Hm(n.End()), strength});
    nightLightStatus->SetPlainText(now + L"\n" + plan);
}

void SettingsWindow::NightMarker()
{
    Settings v = app.Scheduled();   // (Night light's strength, when its schedule is followed)
    blueSlider->SetMarker(v.ScheduleEnabled ? v.NightBlue : -1);
}

void SettingsWindow::NightChanged()
{
    app.Changed(true);
    RefreshStatus();
}

} // namespace bb
