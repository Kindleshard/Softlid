// The other features' cards on the panel: the reading strip, film grain, the focus timer and modes for apps.
#include "features/settings/SettingsWindow.h"
#include "features/settings/SettingsHost.h"
#include "core/Lang.h"
#include "core/Modes.h"
#include "features/settings/AppRuleDialog.h"
#include "platform/ProgramInfo.h"
#include "ui/Theme.h"
#include <algorithm>
#include <cmath>

namespace bb {
namespace {

using namespace ui;

const wchar_t* const ShapeNames[] = {L"Band", L"Column", L"Rectangle", L"Circle"};

std::wstring Px(int v) { return std::to_wstring(v) + L" px"; }

std::vector<std::wstring> SideNames(int shape)
{
    if (shape == 1) return {L"Left and right", L"Left only", L"Right only"};
    return {L"Above and below", L"Above only", L"Below only"};
}

} // namespace

void SettingsWindow::BuildStrip(Stack& st)
{
    FeatureHead(st, FStrip, L"Dims everything but a clear area that follows your mouse.");
    // Its shape, each shown as a little screen with its clear area.
    std::vector<std::wstring> shapeNames(std::begin(ShapeNames), std::end(ShapeNames));
    int lines = TilePicker::CaptionLines(shapeNames, 48 + 13);   // (a longer translation takes two)
    float caption = TilePicker::CaptionHeight(lines);
    Row& shapeRow = st.Add(64 + caption);
    AddLabel(shapeRow, L"Shape", Fonts::Body(), Theme::Fg, 24, 18, 124, 26);
    auto& shape = shapeRow.Add<TilePicker>(4, s.StripShape);
    shape.SetBounds(150, 6, 4 * 48 + 3 * 13 + 2, 48 + caption);
    shape.SetCaptions(shapeNames, lines);
    shape.painter = [this](Canvas& c, const D2D1_RECT_F& r, int i, float awake) { PaintShape(c, r, i, awake); };
    shape.onHover = [&shape](int i) { shape.tip = i >= 0 ? Lang::T(ShapeNames[i]) : L""; };
    SliderRow(st, L"Strip height", 40, 400, s.StripHeight, defaults.StripHeight, Px, [this](int v) { s.StripHeight = v; StripTouched(); },
              [this] { return s.StripShape == 0 || s.StripShape == 2; });
    SliderRow(st, L"Size", 40, 400, s.StripHeight, defaults.StripHeight, Px, [this](int v) { s.StripHeight = v; StripTouched(); },
              [this] { return s.StripShape == 3; });
    SliderRow(st, L"Width", 80, 1600, s.StripWidth, defaults.StripWidth, Px, [this](int v) { s.StripWidth = v; StripTouched(); },
              [this] { return s.StripShape == 1 || s.StripShape == 2; });
    SliderRow(st, L"Dim outside the strip", 30, 95, s.StripDim, defaults.StripDim, [](int v) { return std::to_wstring(v) + L"%"; },
              [this](int v) { s.StripDim = v; StripTouched(); });
    Row& tintRow = st.Add(46);
    AddLabel(tintRow, L"Tint", Fonts::Body(), Theme::Fg, 24, 10, 200, 26);
    auto& tint = tintRow.Add<ColorButton>();
    tint.SetBounds(Edge - 36, 6, 34, 34);
    tint.SetValue(s.StripColor | 0xFF000000);
    tint.presets.assign(std::begin(Palette::StripTints), std::end(Palette::StripTints));
    tint.tip = Lang::T(L"Choose a color");
    tint.onChange = [this, &tint] { s.StripColor = tint.Value(); StripTouched(); };

    Shown more = More(st, L"strip");
    // The clear area's edge: none, soft, or an outline (one or the other).
    Row& edgeRow = st.Add(46, more);
    AddLabel(edgeRow, L"Edge", Fonts::Body(), Theme::Fg, 24, 10, 120, 26);
    auto& edge = edgeRow.Add<Segmented>(std::vector<std::wstring>{L"None", L"Soft", L"Outline"});
    edge.SetBounds(150, 6, Edge - 150, 34);
    edge.SetSelected(s.StripEdge);
    edge.onChange = [this](int v) { s.StripEdge = v; StripTouched(); UpdateRows(); };
    SliderRow(st, L"Softness", 4, 200, s.StripFeather, defaults.StripFeather, Px, [this](int v) { s.StripFeather = v; StripTouched(); },
              And(more, [this] { return s.StripEdge == 1; }));
    Slider& outline = SliderRow(st, L"Outline", 1, 12, s.StripBorder, defaults.StripBorder, Px,
                                [this](int v) { s.StripBorder = v; StripTouched(); }, And(more, [this] { return s.StripEdge == 2; }));
    outline.SetBounds(outline.Bounds().left, outline.Bounds().top, outline.W() - 44, outline.H());   // room for its colour
    auto& line = outline.Parent()->Add<ColorButton>();
    line.SetBounds(Edge - 70, 27, 32, 32);
    line.allowAccent = true;
    line.SetUseAccent(s.StripLine == 0);
    line.SetValue(s.StripLine == 0 ? Theme::Accent() : s.StripLine | 0xFF000000);
    line.tip = Lang::T(L"Outline color");
    line.onChange = [this, &line] { s.StripLine = line.UseAccent() ? 0 : line.Value(); StripTouched(); };
    // A band or column can dim just one side.
    Row& sidesRow = st.Add(46, And(more, [this] { return s.StripShape <= 1; }));
    AddLabel(sidesRow, L"Dimmed sides", Fonts::Body(), Theme::Fg, 24, 10, 120, 26);
    auto& sides = sidesRow.Add<DropDown>(SideNames(s.StripShape));
    sides.SetBounds(150, 6, Edge - 150, 34);
    sides.SetSelected(s.StripCover);
    sides.onChange = [this](int v) { s.StripCover = v; StripTouched(); };
    shape.onChange = [this, &sides](int v) {
        s.StripShape = v;
        sides.SetItems(SideNames(v));
        StripTouched();
        UpdateRows();
    };
    // Following the mouse, or staying put and moving with the keys.
    Row& moveRow = st.Add(46, more);
    AddLabel(moveRow, L"Movement", Fonts::Body(), Theme::Fg, 24, 10, 120, 26);
    auto& move = moveRow.Add<DropDown>(std::vector<std::wstring>{L"Follow the mouse", L"Move with keys"});
    move.SetBounds(150, 6, Edge - 150, 34);
    move.SetSelected(s.StripFollow ? 0 : 1);
    move.onChange = [this](int v) { s.StripFollow = v == 0; StripTouched(); UpdateRows(); };
    Shown keys = And(more, [this] { return !s.StripFollow; });
    Note(st, L"Ctrl+Alt with the arrow keys moves it; Ctrl+Alt with + and - changes its size (see Keyboard shortcuts in Options).", 2, keys);
    SliderRow(st, L"Speed up held keys", 0, 100, s.StripAccel, 50, [](int v) { return v == 0 ? std::wstring(L"Off") : std::to_wstring(v) + L"%"; },
              [this](int v) { s.StripAccel = v; app.Changed(); }, keys);
    ToggleRow(st, L"Stay on one monitor", s.StripOneMonitor, [this](bool v) { s.StripOneMonitor = v; StripTouched(); }, more);
}

void SettingsWindow::BuildGrain(Stack& st)
{
    Shown grainShown = [this] { return s.AdvancedView; };
    FeatureHead(st, FGrain, L"Fine noise over the whole screen, like film.");
    SliderRow(st, L"Grain strength", 5, 100, s.GrainStrength, defaults.GrainStrength, [](int v) { return std::to_wstring(v) + L"%"; },
              [this](int v) { s.GrainStrength = v; GrainTouched(); }, grainShown);
    Row& toneRow = st.Add(44, grainShown);
    AddLabel(toneRow, L"Grain tone", Fonts::Body(), Theme::Fg, 24, 8, 120, 26);
    auto& tone = toneRow.Add<Segmented>(std::vector<std::wstring>{L"Darker", L"Mixed", L"Lighter"});
    tone.SetBounds(150, 4, Edge - 150, 32);
    tone.SetSelected(s.GrainTone);
    tone.onChange = [this](int v) { s.GrainTone = v; GrainTouched(); };
    ToggleRow(st, L"Coarser", s.GrainCoarse, [this](bool v) { s.GrainCoarse = v; GrainTouched(); }, grainShown);
    ToggleRow(st, L"Animated (more power)", s.GrainAnimated, [this](bool v) { s.GrainAnimated = v; GrainTouched(); }, grainShown);
}

// A little dimmed screen with shape i's clear area in it, and a few lines of text showing through.
void SettingsWindow::PaintShape(Canvas& c, const D2D1_RECT_F& r, int i, float awake)
{
    ID2D1RenderTarget* t = c.Target();
    float w = Width(r), h = Height(r);
    gfx::ComPtr<ID2D1RoundedRectangleGeometry> screen;
    gfx::D2D()->CreateRoundedRectangleGeometry({r, h * 0.14f, h * 0.14f}, screen.GetAddressOf());
    t->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), screen.Get()), nullptr);
    c.Fill(r, Palette::Argb(30, 30, 34));
    D2D1_RECT_F clear = i == 0 ? Rect(r.left - 1, r.top + h * 0.36f, w + 2, h * 0.28f)
                      : i == 1 ? Rect(r.left + w * 0.34f, r.top - 1, w * 0.32f, h + 2)
                      : i == 2 ? Rect(r.left + w * 0.12f, r.top + h * 0.27f, w * 0.76f, h * 0.46f)
                               : Rect(r.left + w * 0.2f, r.top + h * 0.2f, w * 0.6f, h * 0.6f);
    int light = static_cast<int>(185 + 50 * awake);
    int clearColor = Palette::Argb(light, light, std::min(255, light + 4));
    if (i == 3) c.FillCircle((clear.left + clear.right) / 2, (clear.top + clear.bottom) / 2, Width(clear) / 2, clearColor);
    else if (i == 2) c.FillRound(clear, Height(clear) * 0.22f, clearColor);
    else c.Fill(clear, clearColor);
    float x0 = std::max(clear.left, r.left) + w * 0.1f, x1 = std::min(clear.right, r.right) - w * 0.1f;
    float cy = (clear.top + clear.bottom) / 2;
    for (int k = -1; k <= 1; k++) {
        float y = cy + k * h * 0.08f - h * 0.012f;
        if (y < clear.top + h * 0.03f || y > clear.bottom - h * 0.05f) continue;
        float a = x0, b = x1;
        if (i == 3) {
            float half = std::sqrt(std::max(0.0f, Width(clear) * Width(clear) / 4 - (y - cy) * (y - cy)));
            a = (clear.left + clear.right) / 2 - half + w * 0.06f;
            b = (clear.left + clear.right) / 2 + half - w * 0.06f;
        }
        if (b > a) c.Fill(Rect(a, y, (b - a) * (k == 1 ? 0.6f : 1.0f), h * 0.025f), Palette::Argb(50, 50, 60), 150 / 255.0f);
    }
    t->PopLayer();
}

void SettingsWindow::StripTouched()
{
    if (!s.StripEnabled) {
        s.StripEnabled = true;
        SyncFeatures();
    }
    app.Changed(true);
}

void SettingsWindow::GrainTouched()
{
    if (!s.GrainEnabled) {
        s.GrainEnabled = true;
        SyncFeatures();
    }
    app.Changed(true);
}

// ---- The focus timer: work, then rest, again and again (the Pomodoro way) ----

void SettingsWindow::BuildFocusTimer(Stack& st)
{
    FeatureHead(st, FFocus, L"Work, then rest, again and again.");
    Row& card = st.Add(162);
    focusRing = &card.Add<FocusRing>();
    focusRing->SetBounds(24, 4, 154, 154);
    focusRing->tip = Lang::T(L"The dots: work periods done, in rounds of four");
    focusRing->progress = [this] {
        Focus f = app.FocusState();
        return f == Focus::Resting ? 1.0f : f == Focus::Off ? 0.0f : 1 - app.FocusLeft() / static_cast<float>(std::max(1, s.FocusWork * 60));
    };
    focusRing->timeText = [this] { return app.FocusState() == Focus::Resting ? Lang::T(L"Rest") : app.FocusText(); };
    focusRing->stateText = [this] {
        Focus f = app.FocusState();
        return Lang::T(f == Focus::Working ? L"Focus" : f == Focus::Paused ? L"Paused" : f == Focus::Resting ? L"Resting" : L"Ready");
    };
    focusRing->ink = [this] {
        Focus f = app.FocusState();
        return f == Focus::Resting ? Theme::Good : f == Focus::Paused ? Theme::Dim : Theme::Accent();
    };
    focusRing->done = [this] { return app.FocusDone() == 0 ? 0 : (app.FocusDone() - 1) % 4 + 1; };
    focusButton = &AddButton(card, L"Start", 200, 24, Edge - 42 - 12 - 200, 42);
    focusButton->SetTint(Theme::Accent());
    focusButton->onClick = [this] {
        if (app.FocusState() == Focus::Working) app.FocusPause();
        else app.FocusStart();
        RefreshStatus();
    };
    auto& stop = card.Add<Button>();
    stop.SetGlyph(L'\uE71A');
    stop.SetBounds(Edge - 42, 24, 42, 42);
    stop.tip = Lang::T(L"Stop");
    stop.onClick = [this] { app.FocusStop(); RefreshStatus(); };
    auto minutes = [](int v) { return Lang::F(L"{0} min", {std::to_wstring(v)}); };
    auto& work = card.Add<Stepper>(L"Work", 5, 90, 5, s.FocusWork, minutes);
    work.SetBounds(200, 86, 122, 62);
    work.onChange = [this](int v) { s.FocusWork = v; app.Changed(); RefreshStatus(); };
    auto& rest = card.Add<Stepper>(L"Rest", 1, 30, 1, s.FocusRest, minutes);
    rest.SetBounds(Edge - 122, 86, 122, 62);
    rest.onChange = [this](int v) { s.FocusRest = v; app.Changed(); };
    Note(st, L"While it runs, it takes over from the look-away and stand-up reminders; blink reminders go on.", 2);
}

// ---- Modes for apps: a mode that applies while an app is in front ----

void SettingsWindow::BuildRules(Stack& st)
{
    Shown advanced = [this] { return s.AdvancedView; };   // (in Simple they stay listed but idle)
    FeatureHead(st, FRules, L"While one of these apps is in front, its mode is used.");
    rulesRow = &st.Add(30, advanced);
    Row& addRow = st.Add(52, advanced);
    AddButton(addRow, L"Add an app...", 24, 8, 170, 34).onClick = [this] {
        std::vector<std::wstring> taken;
        for (const auto& r : Modes::Rules(s)) taken.push_back(r.app);
        AppRuleDialog::Open(Handle(), taken, [this](const Modes::Rule& added) {
            std::vector<Modes::Rule> rules = Modes::Rules(s);
            std::erase_if(rules, [&](const Modes::Rule& r) { return r.app == added.app; });
            rules.push_back(added);
            Modes::SaveRules(s, rules);
            app.Changed();
            ShowRules();
        }, !unfoldAll);
    };
    Note(st, L"The night schedule doesn't change an app's mode, so Editing keeps true color at night too.", 2, advanced);
    ShowRules();
}

// One line per rule: the app's icon, its name with its file under it, its mode (a drop-down) and a remove button.
void SettingsWindow::ShowRules()
{
    rulesRow->Clear();
    std::vector<Modes::Rule> rules = Modes::Rules(s);
    if (rules.empty()) AddLabel(*rulesRow, L"No apps yet.", Fonts::Small(), Theme::Dim, 24, 4, Edge - 24, 22);
    std::vector<std::wstring> modeNames(Modes::Names, Modes::Names + Modes::Custom);   // every mode but Custom (the same as no rule)
    for (size_t i = 0; i < rules.size(); i++) {
        const Modes::Rule rule = rules[i];
        float y = i * 52.0f;
        int iconPx = static_cast<int>(std::lround(24 * Scale()));
        if (HICON icon = ProgramInfo::Icon(rule.path, iconPx)) rulesRow->Add<IconImage>(icon).SetBounds(24, y + 12, 26, 26);
        else rulesRow->Add<Icon>(L'\uE7C4', 14.0f, Theme::Dim).SetBounds(22, y + 10, 30, 30);
        // (Names as they are: an app called "Game" mustn't be translated.)
        AddLabel(*rulesRow, L"", Fonts::Body(), Theme::Fg, 62, y + 5, Edge - 62 - 220, 24).SetPlainText(ProgramInfo::Name(rule.app, rule.path));
        AddLabel(*rulesRow, L"", Fonts::Small(), Theme::Dim, 62, y + 28, Edge - 62 - 220, 18).SetPlainText(rule.app);
        auto& mode = rulesRow->Add<DropDown>(modeNames);
        mode.SetBounds(Edge - 208, y + 9, 162, 34);
        mode.SetSelected(std::clamp(rule.mode, 0, Modes::Custom - 1));
        mode.onChange = [this, rule](int m) {
            std::vector<Modes::Rule> all = Modes::Rules(s);
            for (auto& r : all) if (r.app == rule.app) r.mode = m;
            Modes::SaveRules(s, all);
            app.Changed();
        };
        auto& remove = rulesRow->Add<Button>();
        remove.SetGlyph(L'\uE711');
        remove.SetBounds(Edge - 36, y + 10, 32, 32);
        remove.tip = Lang::T(L"Remove");
        remove.onClick = [this, rule] {
            std::vector<Modes::Rule> all = Modes::Rules(s);
            std::erase_if(all, [&](const Modes::Rule& r) { return r.app == rule.app; });
            Modes::SaveRules(s, all);
            app.Changed();
            Later([this] { ShowRules(); });   // not while this button's own click is running
        };
    }
    rulesRow->SetBounds(0, rulesRow->Bounds().top, rulesRow->W(), rules.empty() ? 30.0f : rules.size() * 52.0f + 4);
    UpdateRows();
}

} // namespace bb
