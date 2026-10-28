#pragma once
#include "core/Settings.h"
#include "platform/ResourceUse.h"
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

class SettingsHost;

// The settings: made when opened and destroyed when closed, so it costs nothing while you aren't looking at it.
// Each tab is a scrolling stack of rows; rows hide and close up for the Simple view and for options that are off.
// This file has the window itself and the row builders; each page is built in its own file (SettingsScreen.cpp,
// SettingsBlink.cpp, SettingsSchedule.cpp, SettingsExtras.cpp, SettingsOptions.cpp).
class SettingsWindow : public ui::DirectWindow {
public:
    // unfoldAll (diagnostics): every section and "More options" shown open, without saving that.
    SettingsWindow(SettingsHost& app, Settings& settings, bool unfoldAll = false);
    ~SettingsWindow() override;

    // Opens it (at: its top left on screen, or null to centre it); optionsPage: straight to the cog's page.
    void Show(const POINT* at, bool optionsPage, HICON icon, bool activate = true);
    POINT Position() const;

    void SyncFromApp();     // shortcuts and the tray menu changed settings while it is open: mirror them
    void RefreshStatus();   // the status lines (also once a second)
    void IdleCheck();       // once a second: left alone for a while, its memory goes back to Windows

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
    // indent: room on the left (for a grip); foldAt: where the chevron goes (0: just left of a switch)
    ui::Row& SectionHead(ui::Stack& st, wchar_t icon, std::wstring_view title, std::wstring_view hint, Shown shown = nullptr,
                         float indent = 0, float foldAt = 0, ui::Label** hintLabel = nullptr);
    ui::Toggle& Header(ui::Stack& st, wchar_t icon, std::wstring_view title, std::wstring_view hint, bool value,
                       std::function<void(bool)> changed, Shown shown = nullptr);
    void Title(ui::Container& row, wchar_t icon, std::wstring_view title, float indent = 0);
    Shown More(ui::Stack& st, const std::wstring& key, Shown shown = nullptr);
    void Divider(ui::Stack& st, Shown shown = nullptr);
    void Note(ui::Stack& st, std::wstring_view text, int lines, Shown shown = nullptr, float left = 24);   // left: RowText under icons
    // icon: a small one in front of the words (a row of Options), or none
    ui::Toggle& ToggleRow(ui::Stack& st, std::wstring_view text, bool value, std::function<void(bool)> changed, Shown shown = nullptr,
                          wchar_t icon = 0);
    ui::Toggle& AddToggle(ui::Container& parent, float x, float y, bool value, std::function<void(bool)> changed);
    ui::Slider& SliderRow(ui::Stack& st, std::wstring_view text, int min, int max, int value, int def,
                          std::function<std::wstring(int)> format, std::function<void(int)> changed, Shown shown = nullptr, wchar_t icon = 0);
    ui::Row& CardHead(ui::Stack& st, wchar_t icon, std::wstring_view title, std::wstring_view hint, Shown shown = nullptr,
                      float room = 0);   // room: kept free at the right (for a button)
    void RowIcon(ui::Container& row, wchar_t icon, float y);   // a row's small icon, in front of its words (at RowText)
    static constexpr float RowText = 54;                         // where a row's words start after its icon
    ui::Button& AddButton(ui::Container& parent, std::wstring_view text, float x, float y, float w, float h);
    static Shown And(Shown a, Shown b) { return [a, b] { return a() && b(); }; }
    bool IsFolded(const std::wstring& key) const;
    void SetFolded(const std::wstring& key, bool folded);
    static int LinesFor(std::wstring_view text, IDWriteTextFormat* font, float width);

    // ---- The panel of features, and their list in Options (SettingsPanel.cpp) ----
    enum Feature { FFilter, FTone, FBlink, FLookAway, FStandUp, FNight, FStrip, FGrain, FFocus, FRules, FeatureCount };
    void BuildTopBar();
    void ShowOptions(bool on);       // the cog: Options in place of the panel, or back
    void BuildPanel(ui::Stack& st);
    void BuildFeatureList(ui::Stack& st);
    void FeatureHead(ui::Stack& st, Feature f, std::wstring_view hint);   // a feature's heading on the panel
    bool FeatureOn(int f) const;
    bool FeatureListed(int f) const;  // (Film grain and Modes for apps are for the Advanced view)
    bool FeatureShown(int f) const;   // made available in Options: its card is on the panel
    void SetShown(int f, bool on);    // off: its card goes, and the feature stops
    void SetFeature(int f, bool on);  // its card's switch: it runs, or stops
    void SyncFeatures();              // the switches (in the list, and on the cards), as the settings are now
    std::wstring FeatureSummary(int f) const;   // what it does now, in a line (under its name on the panel)
    void RefreshSummaries();
    void LoadOrder();
    void DragFeature(int f, int page, float windowY);   // page: 0 the panel, 1 the list in Options
    void DropFeature(int f, int page, float windowY);
    int DropIndex(int page, float windowY) const;       // where in the order of those shown it would land

    // ---- Screen (SettingsScreen.cpp) ----
    void BuildFilter(ui::Stack& st);
    void BuildEffects(ui::Stack& st);
    void PaintEffect(ui::Canvas& c, const D2D1_RECT_F& r, int effect, float awake);
    bool BluePaused() const;
    void ShowEffect();
    void SetFilter(bool on);         // the screen filter switch (also when a control needs it on)
    void FilterTouched();
    void BacklightToggled(bool on);
    std::wstring BluePausedText() const;

    // ---- Tone, on the Screen page (SettingsTone.cpp) ----
    void BuildTone(ui::Stack& st);
    std::vector<ui::ChoiceRow*> BuildToneEngines(ui::Stack& st);
    void BuildToneStrength(ui::Stack& st, std::vector<ui::ChoiceRow*> rows);
    void BuildToneGraph(ui::Stack& st);
    void BuildToneCustom(ui::Stack& st);
    void BuildLutRow(ui::Stack& st);
    void BuildLutSetup(ui::Stack& st);
    void CompactSlider(ui::Container& parent, float y, std::wstring_view text, int value,
                       std::function<std::wstring(int)> format, std::function<void(int)> changed);
    void SetTone(bool on);           // the tone switch (also when a tone control needs it on)
    void ToneChanged();
    std::wstring ToneText() const;
    std::wstring ToneDetail() const;
    void RefreshTone();
    bool LutInstalled() const;
    void UseLutPath(std::wstring path);
    void LutReady(const std::wstring& path);
    void LutInstallProgress(int kind, const std::wstring& message);

    // ---- Reminders (SettingsBlink.cpp) ----
    void BuildBlink(ui::Stack& st);
    void BuildLookAway(ui::Stack& st);
    void BuildStandUp(ui::Stack& st);

    // ---- Schedule (SettingsSchedule.cpp) ----
    void BuildNight(ui::Stack& st);
    void BuildCityPicker(ui::Stack& st);
    void ShowSync();
    void ShowSuggestions(std::vector<std::wstring> names);
    void ChooseCity(const std::wstring& name);
    void BuildNightLight(ui::Stack& st);
    void RefreshNightLight();
    void NightMarker();
    void NightChanged();

    // ---- Extras (SettingsExtras.cpp) ----
    void BuildStrip(ui::Stack& st);
    void BuildGrain(ui::Stack& st);
    void PaintShape(ui::Canvas& c, const D2D1_RECT_F& r, int shape, float awake);
    void StripTouched();
    void GrainTouched();
    void BuildFocusTimer(ui::Stack& st);
    void BuildRules(ui::Stack& st);
    void ShowRules();

    // ---- Options (SettingsOptions.cpp) ----
    void BuildOptionsPage(ui::Stack& st);
    void BuildMiniOptions(ui::Stack& st);
    void BuildUpdates(ui::Stack& st);
    void BuildSupport(ui::Stack& st);
    void RefreshUpdates();
    void BuildResourceUse(ui::Stack& st);
    void RefreshUsage();

    SettingsHost& app;
    Settings& s;
    const Settings defaults;
    const bool unfoldAll;
    ui::ScrollPage* pages[2] = {};   // the panel, and Options
    bool optionsShown = false;
    ui::Button *cogButton = nullptr, *backButton = nullptr;
    ui::Label* pageTitle = nullptr;
    ui::DropMark* marks[2] = {};
    ui::Toggle* featureToggles[FeatureCount] = {};   // in Options: on the panel or not
    ui::Toggle* cardToggles[FeatureCount] = {};      // on the cards: on or off
    ui::Label* summaries[FeatureCount] = {};
    std::vector<int> order;          // the features, in the order chosen (all of them, shown or not)
    std::vector<std::unique_ptr<ui::Stack>> stacks;
    std::vector<std::function<void()>> updaters;
    std::vector<std::function<void()>> later;

    // Controls the app (or another page) changes while the window is open.
    ui::Toggle *hwToggle = nullptr, *coolToggle = nullptr, *awakeToggle = nullptr;
    ui::Slider *brightSlider = nullptr, *blueSlider = nullptr, *backlightSlider = nullptr;
    ui::ModeGrid* modeGrid = nullptr;
    ui::TilePicker* effectPicker = nullptr;
    ui::Label *ruleStatus = nullptr, *hwStatus = nullptr, *nightStatus = nullptr, *syncStatus = nullptr,
              *breakStatus = nullptr, *longStatus = nullptr, *nightLightStatus = nullptr;
    ui::Icon* syncTick = nullptr;
    ui::Label* nightLegend = nullptr;   // under the blue light filter: what its green dot is
    ui::Row* rulesRow = nullptr;
    ui::FocusRing* focusRing = nullptr;
    ui::DayBar* dayBar = nullptr;
    int dayMinute = -1;   // (the bar is drawn again when the minute changes)
    ui::Button *focusButton = nullptr, *titleMini = nullptr;

    // The city picker.
    ui::TextBox* cityBox = nullptr;
    class CityList;
    CityList* cityList = nullptr;
    ui::Row* suggestRow = nullptr;
    bool pickerOpen = false;

    // Tone.
    static constexpr UINT LutInstallMessage = WM_APP + 21;   // from DesktopLUT's download (see LutFiles::Install)
    ui::CurveView* curve = nullptr;
    ui::Label *toneStatus = nullptr, *lutStatus = nullptr, *lutSetupStatus = nullptr;
    ui::TextBox* lutBox = nullptr;
    ui::Button *lutInstall = nullptr, *lutPick = nullptr;
    bool lutSetupOpen = false;

    // Updates.
    ui::Button* updateButton = nullptr;
    ui::Icon* updateMark = nullptr;
    ui::Label* updateStatus = nullptr;
    ui::ProgressLine* updateProgress = nullptr;
    bool shownDownloading = false;

    // Resource use (Options, Advanced view).
    ui::UsageGraph* usageGraph = nullptr;
    ui::Label* usageText = nullptr;
    std::unique_ptr<ResourceMeter> meter;   // (made the first time the section shows)
    bool measuring = false;                 // (not in a diagnostic copy)

    int shownSync = -1, shownRule = -1;
    ULONGLONG lastInput = GetTickCount64();   // the last click, key or mouse move over it
    bool trimmed = false;                     // memory already given back since then
};

} // namespace bb
