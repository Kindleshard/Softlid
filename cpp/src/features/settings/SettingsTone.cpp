// The Tone section of the Screen page. Two separate questions: which engine does the work (Standard, or an
// experimental one, each trading something), and how strong. A strength means about the same white level in every
// engine; Custom (the three curve sliders) exists only for Standard. The Simple view offers Standard at Gentle to
// Strong, without the curve.
#include "features/settings/SettingsWindow.h"
#include "features/settings/SettingsHost.h"
#include "core/Lang.h"
#include "core/ToneCurve.h"
#include "features/tone/LutFiles.h"
#include "platform/FileDialog.h"
#include "platform/Text.h"
#include "ui/Theme.h"
#include <cmath>

namespace bb {
namespace {

using namespace ui;

constexpr int EngineModes[] = {Settings::ToneStandard, Settings::ToneSmartGpu, Settings::ToneSmartLayer, Settings::ToneDesktopLut};

std::vector<std::wstring> Strengths(bool four)
{
    std::vector<std::wstring> steps(ToneCurve::Levels, ToneCurve::Levels + (four ? 4 : 3));
    return steps;
}

std::wstring Off(int v, std::wstring shown) { return v == 0 ? Lang::T(L"Off") : shown; }

} // namespace

void SettingsWindow::BuildTone(Stack& st)
{
    FeatureHead(st, FTone, L"Calms bright areas and deepens darks, on top of Brightness.");
    std::vector<ChoiceRow*> engines = BuildToneEngines(st);
    BuildToneStrength(st, engines);
    BuildToneGraph(st);
    BuildToneCustom(st);
    BuildLutRow(st);
}

// Which engine does the work: Standard, or an experimental one (Advanced only), with the test lab.
std::vector<ChoiceRow*> SettingsWindow::BuildToneEngines(Stack& st)
{
    Shown advanced = [this] { return s.AdvancedView; };
    Row& engineHead = st.Add(32, advanced);
    AddLabel(engineHead, L"Engine", Fonts::Small(), Theme::Dim, 24, 8, 200, 18);
    Button& lab = AddButton(engineHead, L"Test lab...", Edge - 96, 2, 96, 26);
    lab.tip = Lang::T(L"Compare every engine and strength on a test picture and rate them");
    lab.onClick = [this] { app.OpenToneLab(); };   // (it closes this window)

    struct Engine { const wchar_t *title, *note, *tag, *tip; };
    static const Engine engines[] = {
        {L"Standard", L"No delay, no extra work, works everywhere.", L"", L""},
        {L"Smart (GPU)", L"Best picture; about one frame of delay, a few watts.", L"Experimental",
         L"Looks at what surrounds every pixel: big bright areas are calmed while text, edges and colours keep their contrast."},
        {L"Smart layer", L"Lighter on power; can leave faint trails when moving.", L"Experimental",
         L"The smart filter's dimming as one layer over the screen; it redraws only when something changed."},
        {L"DesktopLUT", L"No delay, hue-true; needs DesktopLUT and admin rights.", L"Experimental",
         L"A free program that applies a colour table inside Windows. Softlid guards it: a 15-second check, "
         L"a crash watchdog and the panic key Ctrl+Alt+Shift+L all put the screen back."},
    };
    std::vector<ChoiceRow*> rows;
    for (int i = 0; i < 4; i++) {
        int mode = EngineModes[i];
        if (i == 1) {
            Row& caption = st.Add(24, advanced);
            AddLabel(caption, L"Experimental: each one trades something", Fonts::Small(), Theme::Dim, 24, 4, Edge - 24, 18);
        }
        Row& r = st.Add(58, advanced);
        auto& row = r.Add<ChoiceRow>(engines[i].title, engines[i].note, engines[i].tag);
        row.SetBounds(16, 2, Edge - 8, 54);
        if (*engines[i].tip) row.tip = Lang::T(engines[i].tip);
        row.onChosen = [this, mode] {
            if (s.ToneMode == mode) return;
            if (mode == Settings::ToneDesktopLut && !LutInstalled()) {
                lutSetupOpen = true;   // DesktopLUT is a separate download: offer to set it up first
                UpdateRows();
                return;
            }
            app.SetToneMode(mode);   // also leaves Custom, which only Standard has
            UpdateRows();
            ToneChanged();
        };
        rows.push_back(&row);
        if (mode == Settings::ToneDesktopLut) BuildLutSetup(st);
    }
    return rows;
}

// How strong (the same white level in every engine), or Custom (Standard only); keeps the engine list in line too.
void SettingsWindow::BuildToneStrength(Stack& st, std::vector<ChoiceRow*> rows)
{
    Row& strengthHead = st.Add(26);
    AddLabel(strengthHead, L"Strength", Fonts::Small(), Theme::Dim, 24, 6, 200, 18);
    Row& strengthRow = st.Add(56);
    auto& strength = strengthRow.Add<StepSlider>(Strengths(s.AdvancedView));
    strength.SetBounds(16, 2, Edge - 16 - 100, 50);
    strength.onChange = [this](int i) {
        if (i < 0) return;
        s.ComfortLevel = i;
        UpdateRows();
        ToneChanged();
    };
    Button& custom = AddButton(strengthRow, L"Custom", Edge - 84, 8, 84, 30);
    custom.tip = Lang::T(L"Set the curve yourself with three sliders (Standard engine only)");
    custom.onClick = [this] {
        if (s.CustomTone()) return;
        s.ComfortLevel = Settings::CustomLevel;
        UpdateRows();
        ToneChanged();
    };
    // Keeps the engine list, the strength steps and the Custom button in line with the settings.
    updaters.push_back([this, rows, &strength, &custom] {
        for (int i = 0; i < 4; i++) rows[i]->SetChecked(EngineModes[i] == s.ToneMode);
        strength.SetSteps(Strengths(s.AdvancedView));
        strength.SetSelected(s.CustomTone() ? -1 : s.Level());
        custom.SetVisible(s.ToneMode == Settings::ToneStandard && s.AdvancedView);
        custom.SetTint(s.CustomTone() ? Theme::Accent() : 0);
    });
}

void SettingsWindow::BuildToneGraph(Stack& st)
{
    // Below the strength: the graph and status (and, for Standard, adapting to white screens) ...
    Row& graphRow = st.Add(108, [this] { return s.AdvancedView && !s.CustomTone() && s.ToneMode != Settings::ToneDesktopLut; });
    curve = &graphRow.Add<CurveView>();
    curve->SetBounds(24, 4, 92, 92);
    curve->curve = [this](double x) {
        if (s.CustomTone()) return ToneCurve::Net(x, s.ShadowCrush, s.Squeeze, s.LimiterStrength);
        return s.ToneMode == Settings::ToneStandard ? app.ComfortNet(x) : ToneCurve::Flat(s.Level(), x);
    };
    curve->tip = Lang::T(L"Input brightness (left to right) against what you see (bottom to top)");
    toneStatus = &AddLabel(graphRow, L"", Fonts::Small(), Theme::Dim, 128, 2, Edge - 128, 66);
    toneStatus->wrap = toneStatus->top = true;
    Label& adaptText = AddLabel(graphRow, L"Dim more when the screen is mostly white", Fonts::Small(), Theme::Fg, 128, 74, Edge - 128 - 50, 22);
    Toggle& adapt = AddToggle(graphRow, Edge - 44, 73, s.AdaptToContent, [this](bool v) {
        s.AdaptToContent = v;
        ToneChanged();
    });
    updaters.push_back([this, &adaptText, &adapt] {   // (adapting is Standard's)
        adaptText.SetVisible(s.ToneMode == Settings::ToneStandard);
        adapt.SetVisible(s.ToneMode == Settings::ToneStandard);
    });
}

void SettingsWindow::BuildToneCustom(Stack& st)
{
    // ... or the custom sliders ...
    Row& customRow = st.Add(106, [this] { return s.CustomTone(); });
    auto percent = [](int v) { return Off(v, std::to_wstring(v) + L"%"); };
    CompactSlider(customRow, 2, L"Darken shadows", s.ShadowCrush, percent, [this](int v) {
        s.ShadowCrush = v;
        ToneChanged();
    });
    CompactSlider(customRow, 35, L"Squeeze highlights", s.Squeeze, percent, [this](int v) {
        s.Squeeze = v;
        ToneChanged();
    });
    CompactSlider(customRow, 68, L"Limit whites", s.LimiterStrength,
        [](int v) { return Off(v, Lang::F(L"to {0:0}%", {std::to_wstring(std::lround(ToneCurve::CapFor(v) * 100))})); },
        [this](int v) {
            s.LimiterStrength = v;
            ToneChanged();
        });
}

void SettingsWindow::BuildLutRow(Stack& st)
{
    // ... or where DesktopLUT is, the 15-second check, and its status.
    Row& lutRow = st.Add(108, [this] { return s.ToneMode == Settings::ToneDesktopLut; });
    lutBox = &lutRow.Add<TextBox>();
    lutBox->SetBounds(24, 4, Edge - 24 - 104, 28);
    lutBox->SetText(s.LutPath);
    lutBox->tip = Lang::T(L"Where DesktopLUT.exe is");
    lutBox->onLeave = [this] { UseLutPath(lutBox->Text()); };
    lutBox->onKey = [this](UINT key) {
        if (key == VK_RETURN) UseLutPath(lutBox->Text());
    };
    AddButton(lutRow, L"Browse...", Edge - 96, 3, 96, 30).onClick = [this] {
        std::wstring path = s.LutPath;
        if (!FileDialog::PickProgram(Handle(), Lang::T(L"Where is DesktopLUT?"), L"DesktopLUT", path)) return;
        lutBox->SetText(path);
        UseLutPath(path);
    };
    AddButton(lutRow, L"Run the 15-second check again", Edge - 230, 42, 230, 30).onClick = [this] {
        app.RetestLut();
        SyncFromApp();
        RefreshStatus();
    };
    lutStatus = &AddLabel(lutRow, L"Starting...", Fonts::Small(), Theme::Dim, 24, 40, Edge - 24 - 240, 64);
    lutStatus->wrap = lutStatus->top = true;
}

bool SettingsWindow::LutInstalled() const { return GetFileAttributesW(s.LutPath.c_str()) != INVALID_FILE_ATTRIBUTES; }

// DesktopLUT is optional and separate: until it is set up, choosing it opens this instead of switching to it.
void SettingsWindow::BuildLutSetup(Stack& st)
{
    Row& r = st.Add(132, [this] { return lutSetupOpen && !LutInstalled(); });
    std::wstring page = LutFiles::Page;
    Label& text = AddLabel(r, L"", Fonts::Small(), Theme::Dim, 48, 2, Edge - 48, 54);
    text.SetPlainText(Lang::F(L"DesktopLUT is a separate free program ({0}, about 1 MB). Softlid can download its latest release and set "
                              L"it up in your user folder. It asks for administrator permission when it starts, and Softlid guards it "
                              L"with a 15-second check.", {page.substr(8)}));
    text.wrap = text.top = true;
    lutInstall = &AddButton(r, L"Download and set up", 48, 60, 170, 32);
    lutPick = &AddButton(r, L"I have it...", 226, 60, 110, 32);
    AddButton(r, L"Not now", 344, 60, Edge - 344, 32).onClick = [this] {
        lutSetupOpen = false;
        UpdateRows();
    };
    lutSetupStatus = &AddLabel(r, L"Nothing is downloaded until you choose.", Fonts::Small(), Theme::Dim, 48, 98, Edge - 48, 32);
    lutSetupStatus->wrap = lutSetupStatus->top = true;
    lutInstall->onClick = [this] {
        lutInstall->SetEnabled(false);
        lutPick->SetEnabled(false);
        LutFiles::Install(Handle(), LutInstallMessage);
    };
    lutPick->onClick = [this] {
        std::wstring path = LutFiles::Folder() + L"\\DesktopLUT.exe";
        if (!FileDialog::PickProgram(Handle(), Lang::T(L"Where is DesktopLUT?"), L"DesktopLUT", path)) return;
        LutReady(path);
    };
}

// From the download (kind 0: progress, 1: set up, 2: failed).
void SettingsWindow::LutInstallProgress(int kind, const std::wstring& message)
{
    lutSetupStatus->SetText(message);
    if (kind == 0) return;
    lutInstall->SetEnabled(true);
    lutPick->SetEnabled(true);
    if (kind == 1) LutReady(LutFiles::Folder() + L"\\DesktopLUT.exe");
}

void SettingsWindow::LutReady(const std::wstring& path)
{
    s.LutPath = path;
    s.LutConfirmedBuild.clear();   // a new copy gets the 15-second check
    lutBox->SetText(path);
    lutSetupOpen = false;
    app.SetToneMode(4);            // starts it: the permission prompt and the 15-second check follow
    UpdateRows();
    ToneChanged();
}

void SettingsWindow::UseLutPath(std::wstring path)
{
    path = Trim(path);
    if (path.size() >= 2 && path.front() == L'"' && path.back() == L'"') path = path.substr(1, path.size() - 2);
    if (path == s.LutPath) return;
    s.LutPath = path;
    app.RetestLut();   // a different program gets the 15-second check (and this saves the path)
    SyncFromApp();
    RefreshStatus();
}

// A label, a slider and its value on one line, for the custom tone sliders.
void SettingsWindow::CompactSlider(Container& parent, float y, std::wstring_view text, int value,
                                   std::function<std::wstring(int)> format, std::function<void(int)> changed)
{
    AddLabel(parent, text, Fonts::Small(), Theme::Fg, 24, y, 140, 30);
    Label& shown = AddLabel(parent, L"", Fonts::Small(), Theme::Dim, Edge - 70, y, 70, 30);
    shown.SetPlainText(format(value));
    shown.align = HAlign::Right;
    auto& slider = parent.Add<Slider>(0, 100, value);
    slider.SetBounds(164, y, Edge - 164 - 74, 30);
    slider.onChange = [&shown, format, changed](int v) {
        shown.SetPlainText(format(v));
        changed(v);
    };
}

void SettingsWindow::SetTone(bool on)
{
    s.ToneEnabled = on;
    SyncFeatures();
    app.ManualScreenChange();   // tone is part of a mode (Editing turns it off)
    if (on && !s.FilterEnabled) SetFilter(true);
    else app.Changed(true);
    curve->Invalidate();
    RefreshStatus();
}

// A tone control does nothing while tone or the whole screen filter is off, so touching one switches them on.
void SettingsWindow::ToneChanged()
{
    if (!s.ToneEnabled) {
        SetTone(true);
        return;
    }
    FilterTouched();
    curve->Invalidate();
    RefreshStatus();
}

std::wstring SettingsWindow::ToneText() const
{
    std::wstring text = ToneDetail();
    return !app.GuardNote().empty() && s.FilterEnabled ? app.GuardNote() + L"\n" + text : text;
}

std::wstring SettingsWindow::ToneDetail() const
{
    if (!s.FilterEnabled) return Lang::T(L"The screen filter is off, so nothing is applied.");
    if (!s.ToneEnabled) return Lang::T(L"Tone is off. Brightness and the blue light filter still apply.");
    if (s.CustomTone()) {
        bool flat = s.ShadowCrush == 0 && s.Squeeze == 0 && s.LimiterStrength == 0;
        if (flat) return Lang::T(L"All three sliders are at zero, so nothing changes.");
        std::wstring status = app.ToneStatus();
        for (size_t at; (at = status.find(L"    ")) != std::wstring::npos;) status.replace(at, 4, L"\n");
        return Lang::T(L"Right now:") + L"\n" + status;
    }
    if (s.ToneMode == Settings::ToneSmartGpu) return app.SmartStatus() + L"\n" + Lang::T(L"Graph: a large flat area. Details keep their contrast.");
    if (s.ToneMode == Settings::ToneSmartLayer) {
        std::wstring status = app.LayerStatus();
        return (status.empty() ? Lang::T(L"Measuring...") : status) + L"\n" + Lang::T(L"Graph: a large flat area.");
    }
    if (s.ToneMode == Settings::ToneDesktopLut) {
        std::wstring status = app.LutStatus();
        return (status.empty() ? Lang::T(L"Starting...") : status) +
               (app.LutFailed() ? L" " + Lang::T(L"Standard is used meanwhile.") : L"\n" + Lang::T(L"Panic key: Ctrl+Alt+Shift+L."));
    }
    return app.ComfortStatus();
}

void SettingsWindow::RefreshTone()
{
    toneStatus->SetPlainText(ToneText());
    std::wstring lut = app.LutStatus();
    lutStatus->SetPlainText(lut.empty() ? Lang::T(L"Starting...") : lut);
    curve->Invalidate();
}

} // namespace bb
