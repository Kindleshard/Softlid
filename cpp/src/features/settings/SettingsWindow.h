#pragma once
#include "core/Settings.h"
#include "ui/DirectWindow.h"
#include "ui/Layout.h"
#include "ui/controls/Basic.h"
#include "ui/controls/Choices.h"
#include "ui/controls/ColorPicker.h"
#include "ui/controls/Pickers.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace bb {

class App;

// The settings: made when opened and destroyed when closed, so it costs nothing while you aren't looking at it.
// Each tab is a scrolling stack of rows; rows hide and close up for the Simple view and for options that are off.
// This file has the window itself and the row builders; each page is built in its own file (SettingsScreen.cpp,
// SettingsBlink.cpp, SettingsSchedule.cpp, SettingsExtras.cpp, SettingsOptions.cpp).
class SettingsWindow : public ui::DirectWindow {
public:
    // unfoldAll (diagnostics): every section and "More options" shown open, without saving that.
    SettingsWindow(App& app, Settings& settings, bool unfoldAll = false);
    ~SettingsWindow() override;

    // Opens it (at: its top left on screen, or null to centre it); optionsPage: straight to the cog's page.
    void Show(const POINT* at, bool optionsPage, HICON icon, bool activate = true);
    POINT Position() const;

    void SyncFromApp();     // shortcuts and the tray menu changed settings while it is open: mirror them
    void RefreshStatus();   // the status lines (also once a second)

protected:
    void Layout() override;
    LRESULT OnMessage(UINT message, WPARAM w, LPARAM l) override;

private:
    using Shown = std::function<bool()>;
    static constexpr float W = 480, Edge = 456, H = 620;   // client width, right content edge, height (96-dpi units)

    // ---- Layout ----
    void UpdateRows();               // which rows show, and where (each page adds its part to `updaters`)
    void Later(std::function<void()> work);   // after the current click is over (e.g. a button removing itself)
    void CloseClicked();             // the close button: ask, keep running in the tray, or exit (as set in Options)

    // ---- Row builders ----
    ui::Label& AddLabel(ui::Container& parent, std::wstring_view text, IDWriteTextFormat* font, int color,
                        float x, float y, float w, float h);
    ui::Row& SectionHead(ui::Stack& st, wchar_t icon, std::wstring_view title, std::wstring_view hint, Shown shown = nullptr);
    ui::Toggle& Header(ui::Stack& st, wchar_t icon, std::wstring_view title, std::wstring_view hint, bool value,
                       std::function<void(bool)> changed, Shown shown = nullptr);
    void Title(ui::Container& row, wchar_t icon, std::wstring_view title);
    Shown More(ui::Stack& st, const std::wstring& key, Shown shown = nullptr);
    void Divider(ui::Stack& st, Shown shown = nullptr);
    void Note(ui::Stack& st, std::wstring_view text, int lines, Shown shown = nullptr);
    ui::Toggle& ToggleRow(ui::Stack& st, std::wstring_view text, bool value, std::function<void(bool)> changed, Shown shown = nullptr);
    ui::Toggle& AddToggle(ui::Container& parent, float x, float y, bool value, std::function<void(bool)> changed);
    ui::Slider& SliderRow(ui::Stack& st, std::wstring_view text, int min, int max, int value, int def,
                          std::function<std::wstring(int)> format, std::function<void(int)> changed, Shown shown = nullptr);
    ui::Button& AddButton(ui::Container& parent, std::wstring_view text, float x, float y, float w, float h);
    static Shown And(Shown a, Shown b) { return [a, b] { return a() && b(); }; }
    bool IsFolded(const std::wstring& key) const;
    void SetFolded(const std::wstring& key, bool folded);
    static int LinesFor(std::wstring_view text, IDWriteTextFormat* font, float width);

    // ---- Screen (SettingsScreen.cpp) ----
    void BuildScreenPage(ui::Stack& st);
    void BuildEffects(ui::Stack& st);
    void PaintEffect(ui::Canvas& c, const D2D1_RECT_F& r, int effect, float awake);
    bool EffectPausesBlue() const;
    void ShowEffect();
    void SetFilter(bool on);         // the screen filter switch (also when a control needs it on)
    void FilterTouched();
    void BacklightToggled(bool on);
    std::wstring EffectPausedText() const;

    // ---- Blink (SettingsBlink.cpp) ----
    void BuildBlinkPage(ui::Stack& st);

    // ---- Schedule (SettingsSchedule.cpp) ----
    void BuildSchedulePage(ui::Stack& st);
    void BuildCityPicker(ui::Stack& st);
    void ShowSync();
    void ShowSuggestions(std::vector<std::wstring> names);
    void ChooseCity(const std::wstring& name);
    void NightMarker();
    void NightChanged();

    // ---- Extras (SettingsExtras.cpp) ----
    void BuildExtrasPage(ui::Stack& st);
    void PaintShape(ui::Canvas& c, const D2D1_RECT_F& r, int shape, float awake);
    void StripTouched();
    void GrainTouched();
    void BuildRules(ui::Stack& st);
    void ShowRules();

    // ---- Options (SettingsOptions.cpp) ----
    void BuildOptionsPage(ui::Stack& st);
    void BuildMiniOptions(ui::Stack& st);
    void BuildUpdates(ui::Stack& st);

    App& app;
    Settings& s;
    const Settings defaults;
    const bool unfoldAll;
    ui::TabStrip* tabs = nullptr;
    ui::ScrollPage* pages[5] = {};
    std::vector<std::unique_ptr<ui::Stack>> stacks;
    std::vector<std::function<void()>> updaters;
    std::vector<std::function<void()>> later;

    // Controls the app (or another page) changes while the window is open.
    ui::Toggle *filterToggle = nullptr, *hwToggle = nullptr, *stripToggle = nullptr, *grainToggle = nullptr,
               *coolToggle = nullptr, *awakeToggle = nullptr;
    ui::Slider *brightSlider = nullptr, *blueSlider = nullptr, *backlightSlider = nullptr;
    ui::ModeGrid* modeGrid = nullptr;
    ui::TilePicker* effectPicker = nullptr;
    ui::Label *ruleStatus = nullptr, *hwStatus = nullptr, *nightStatus = nullptr, *syncStatus = nullptr,
              *breakStatus = nullptr, *longStatus = nullptr;
    ui::Icon* syncTick = nullptr;
    ui::Row* rulesRow = nullptr;

    // The city picker.
    ui::TextBox* cityBox = nullptr;
    class CityList;
    CityList* cityList = nullptr;
    ui::Row* suggestRow = nullptr;
    bool pickerOpen = false;
    int shownSync = -1, shownRule = -1;
};

} // namespace bb
