// The screen filter's card on the panel: its modes, colour effects and the backlight; tone (its own card), with its engines, is in
// SettingsTone.cpp.
#include "features/settings/SettingsWindow.h"
#include "features/settings/SettingsHost.h"
#include "core/ColorMath.h"
#include "core/Lang.h"
#include "core/Modes.h"
#include "ui/Theme.h"
#include <algorithm>

namespace bb {
namespace {

using namespace ui;

const wchar_t* const EffectNames[] = {L"Original", L"Grayscale", L"Sepia", L"E-ink", L"Red night"};

std::wstring Percent(int v) { return std::to_wstring(v) + L"%"; }

} // namespace

void SettingsWindow::BuildFilter(Stack& st)
{
    FeatureHead(st, FFilter, L"Ctrl+Alt+Shift with F: on/off · ↑ ↓: brightness · ← →: warmth");
    // One-click modes, as one bar of segments (each its icon in its own soft colour, over its name); moving a slider
    // makes it your own (Custom) again. Not Pause: the card's switch does that (the segments are modes 1 on, so
    // segment i is mode i + 1).
    std::vector<std::wstring> glyphs, names;
    std::vector<int> tints;
    for (int i = Modes::Health; i < Modes::Count; i++) {
        glyphs.push_back(Modes::Glyphs[i]);
        names.push_back(Modes::Names[i]);
        tints.push_back(Palette::ModeTints[i]);
    }
    float barH = ModeGrid::BarHeight();
    Row& modeRow = st.Add(barH + 14);
    modeGrid = &modeRow.Add<ModeGrid>(glyphs, names);
    modeGrid->bar = true;
    modeGrid->tints = tints;
    modeGrid->SetBounds(24, 6, Edge - 24, barH);
    modeGrid->SetSelected(std::max(-1, s.Mode - 1));
    modeGrid->onChange = [this](int chip) {
        app.ChooseMode(chip + 1);
        SyncFromApp();
    };
    Row& ruleRow = st.Add(26, [this] { return app.RuleMode() >= 0; });
    ruleStatus = &AddLabel(ruleRow, L"A rule is in use.", Fonts::Small(), Theme::Accent(), 24, 2, Edge - 24, 22);

    brightSlider = &SliderRow(st, L"Brightness", 20, 100, s.Brightness, defaults.Brightness, Percent, [this](int v) {
        s.Brightness = v;
        app.ManualScreenChange();
        FilterTouched();
    });
    blueSlider = &SliderRow(st, L"Blue light filter", s.CoolRange ? -75 : 0, 100, s.BlueFilter, defaults.BlueFilter,
        [](int v) { return v == 0 ? std::wstring(L"Off") : std::to_wstring(ColorMath::KelvinFor(v)) + L" K"; },
        [this](int v) {
            s.BlueFilter = v;
            if (s.ColorEffect != ColorMath::NoEffect) {   // they take turns
                s.ColorEffect = ColorMath::NoEffect;
                ShowEffect();
            }
            app.ManualScreenChange();
            app.PreviewNight();
            FilterTouched();
        });
    blueSlider->warm = true;
    brightSlider->shade = true;
    // What the green dot on its track is: the night schedule's strength (while the schedule is on).
    Row& legendRow = st.Add(24, [this] { return app.Scheduled().ScheduleEnabled; });
    legendRow.Add<Icon>(L'', 7.0f, Theme::Good).SetBounds(24, 0, 14, 20);   // a dot
    nightLegend = &AddLabel(legendRow, L"", Fonts::Small(), Theme::Dim, 42, 0, Edge - 42, 20);
    NightMarker();
    // A colour effect and the blue light filter take turns: while an effect is on, the slider is greyed and says so
    // (moving it brings the blue light filter back and the effect goes back to Original). Windows Night light, while
    // it is on, takes its place the same way.
    Row& pausedRow = st.Add(26, [this] { return BluePaused(); });
    Label& paused = AddLabel(pausedRow, L"", Fonts::Small(), Theme::Dim, 24, 0, Edge - 24, 22);
    updaters.push_back([this, &paused] {
        blueSlider->SetMuted(BluePaused());
        paused.SetPlainText(BluePausedText());
    });

    // The real backlight: a switch, then a slider of its own; Brightness above always dims pixels.
    hwToggle = &ToggleRow(st, L"Dim with the display's real backlight", s.HardwareBrightness, [this](bool on) { BacklightToggled(on); });
    backlightSlider = &SliderRow(st, L"Display backlight", 0, 100, std::max(0, s.Backlight), 100, Percent, [this](int v) {
        s.Backlight = v;
        app.Changed(true);
    }, [this] { return s.HardwareBrightness; });
    backlightSlider->shade = true;
    Row& hwRow = st.Add(26, [this] { return s.HardwareBrightness || !app.BacklightStatus().empty(); });
    hwStatus = &AddLabel(hwRow, L"", Fonts::Small(), Theme::Dim, 24, 2, Edge - 24, 20);
    BuildEffects(st);

    Shown advanced = More(st, L"screen", [this] { return s.AdvancedView; });   // (all it holds is for the Advanced view)
    coolToggle = &ToggleRow(st, L"Allow cooler than daylight (up to 10 000 K)", s.CoolRange, [this](bool v) {
        s.CoolRange = v;
        if (!v && s.BlueFilter < 0) s.BlueFilter = 0;
        blueSlider->SetRange(v ? -75 : 0, 100);
        blueSlider->SetValue(s.BlueFilter);
        app.Changed(true);
    }, advanced);
    ToggleRow(st, L"Invert colors", s.Invert, [this](bool v) {
        s.Invert = v;
        FilterTouched();
    }, advanced);
}

// ---- Colour effects: each shown as a little picture through it ----

void SettingsWindow::BuildEffects(Stack& st)
{
    std::vector<std::wstring> names(std::begin(EffectNames), std::end(EffectNames));
    int lines = TilePicker::CaptionLines(names, 48 + 13);   // (a longer translation takes two)
    float caption = TilePicker::CaptionHeight(lines);
    // Folded into one line (the effect chosen, and a chevron); a click on it shows the pictures to choose from.
    static bool effectsOpen = false;   // (for as long as BlinkBuddy runs)
    Shown open = [this] { return unfoldAll || effectsOpen; };
    Row& line = st.Add(48);
    line.Add<ui::Divider>().SetBounds(24, 4, Edge - 24, 1);
    AddLabel(line, L"Color effect", Fonts::Body(), Theme::Fg, 24, 14, 200, 26);
    Label& chosen = AddLabel(line, L"", Fonts::Body(), Theme::Dim, 200, 14, Edge - 200 - 26, 26);
    chosen.align = HAlign::Right;
    Icon& chevron = line.Add<Icon>(L'', 8.0f, Theme::Dim);
    chevron.SetBounds(Edge - 20, 16, 20, 22);
    auto& area = line.Add<HotArea>();
    area.SetBounds(20, 8, Edge - 20, 38);
    area.onClick = [this] {
        effectsOpen = !effectsOpen;
        UpdateRows();
    };
    updaters.push_back([this, &chosen, &chevron, open] {
        chosen.SetPlainText(Lang::T(EffectNames[std::clamp(s.ColorEffect, 0, 4)]));
        chevron.Set(open() ? L'' : L'', Theme::Dim);   // down when open, right when folded
    });
    Row& row = st.Add(60 + caption, open);
    int center = static_cast<int>((24 + Edge) / 2);
    effectPicker = &row.Add<TilePicker>(5, s.ColorEffect);
    effectPicker->SetBounds(center - (5 * 48 + 4 * 13 + 2) / 2.0f, 2, 5 * 48 + 4 * 13 + 2, 48 + caption);
    effectPicker->SetCaptions(names, lines);
    effectPicker->painter = [this](Canvas& c, const D2D1_RECT_F& r, int i, float awake) { PaintEffect(c, r, i, awake); };
    effectPicker->onChange = [this](int effect) {
        s.ColorEffect = effect;
        app.ManualScreenChange();
        FilterTouched();
        UpdateRows();
    };
    effectPicker->onHover = [this](int i) { effectPicker->tip = i >= 0 ? Lang::T(EffectNames[i]) : L""; };
    Row& styleRow = st.Add(46, And(open, [this] { return s.AdvancedView && s.ColorEffect == ColorMath::Grayscale; }));
    AddLabel(styleRow, L"Grayscale", Fonts::Body(), Theme::Fg, 24, 10, 124, 26);
    auto& style = styleRow.Add<Segmented>(std::vector<std::wstring>{L"Rec.709", L"Rec.601", L"Black & white"});
    style.SetBounds(150, 6, Edge - 150, 34);
    style.SetSelected(s.GrayStyle);
    style.onChange = [this](int v) {
        s.GrayStyle = v;
        FilterTouched();
        effectPicker->Invalidate();
    };
    Note(st, L"Color effects need the Standard, Smart layer or DesktopLUT engine; Smart (GPU) doesn't show them.", 2,
         [this] { return s.FilterEnabled && !app.EffectsAvailable(); });
    updaters.push_back([this] { effectPicker->SetEnabled(app.EffectsAvailable()); });
}

// A small scene (sky, sun, hill, a red flower, lines of text) as it looks through effect i.
void SettingsWindow::PaintEffect(Canvas& c, const D2D1_RECT_F& r, int effect, float)
{
    auto through = [this, effect](int argb) {
        int red = (argb >> 16) & 0xFF, green = (argb >> 8) & 0xFF, blue = argb & 0xFF;
        ColorMath::Through(effect, s.GrayStyle, red, green, blue);
        return Palette::Argb(red, green, blue);
    };
    ID2D1RenderTarget* t = c.Target();
    float n = Width(r), x = r.left, y = r.top;
    gfx::ComPtr<ID2D1RoundedRectangleGeometry> clip;
    gfx::D2D()->CreateRoundedRectangleGeometry({r, n * 0.18f, n * 0.18f}, clip.GetAddressOf());
    t->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clip.Get()), nullptr);
    // The sky: an evening gradient (an effect is a straight mix of channels, so the ends can go through it alone).
    D2D1_GRADIENT_STOP stops[] = {{0, gfx::Color(through(Palette::Argb(70, 140, 230)))}, {1, gfx::Color(through(Palette::Argb(250, 170, 110)))}};
    gfx::ComPtr<ID2D1GradientStopCollection> collection;
    gfx::ComPtr<ID2D1LinearGradientBrush> sky;
    t->CreateGradientStopCollection(stops, 2, collection.GetAddressOf());
    t->CreateLinearGradientBrush({{x, y}, {x, y + n}}, collection.Get(), sky.GetAddressOf());
    t->FillRectangle(r, sky.Get());
    auto ellipse = [&](float ex, float ey, float ew, float eh, int argb) {
        t->FillEllipse({{x + ex + ew / 2, y + ey + eh / 2}, ew / 2, eh / 2}, c.Brush(through(argb)));
    };
    ellipse(n * 0.58f, n * 0.14f, n * 0.24f, n * 0.24f, Palette::Argb(255, 230, 120));    // sun
    ellipse(-n * 0.3f, n * 0.62f, n * 1.6f, n * 0.8f, Palette::Argb(70, 160, 80));        // hill
    ellipse(n * 0.2f, n * 0.66f, n * 0.14f, n * 0.14f, Palette::Argb(225, 50, 60));       // flower
    c.Fill(Rect(x + n * 0.08f, y + n * 0.12f, n * 0.38f, n * 0.34f), through(Palette::Argb(245, 245, 240)));   // a page
    for (int k = 0; k < 4; k++)
        c.Fill(Rect(x + n * 0.12f, y + n * (0.17f + k * 0.07f), n * (k == 3 ? 0.18f : 0.3f), n * 0.025f), through(Palette::Argb(40, 40, 50)));
    t->PopLayer();
}

// Whether the blue light filter is resting right now: Windows Night light is warming the screen, or a colour effect
// is taking its place.
bool SettingsWindow::BluePaused() const
{
    return app.NightLightWarming() || (s.FilterEnabled && s.ColorEffect != ColorMath::NoEffect && app.EffectsAvailable());
}

std::wstring SettingsWindow::BluePausedText() const
{
    if (app.NightLightWarming()) return Lang::T(L"Paused while Windows Night light warms the screen");
    int i = std::clamp(s.ColorEffect, 0, 4);
    return Lang::F(L"Paused while {0} is on", {Lang::T(EffectNames[i])});
}

// The effect tiles following the settings (e.g. after the blue light slider put the effect back to Original).
void SettingsWindow::ShowEffect()
{
    effectPicker->SetSelected(s.ColorEffect);
    UpdateRows();
}

void SettingsWindow::SetFilter(bool on)
{
    s.FilterEnabled = on;
    SyncFeatures();
    app.ManualScreenChange();
    app.Changed(true);
    UpdateRows();
}

// Moving a filter control while the filter is off would do nothing visible, so switch it on.
void SettingsWindow::FilterTouched()
{
    if (!s.FilterEnabled) SetFilter(true);
    else app.Changed(true);
    modeGrid->SetSelected(std::max(-1, s.Mode - 1));
}

void SettingsWindow::BacklightToggled(bool on)
{
    hwStatus->SetText(on ? L"Checking your displays..." : L"");
    UpdateRows();
    app.SetHardwareBrightness(on, [this] {
        hwToggle->SetOn(s.HardwareBrightness);   // stays off if no display answered
        hwStatus->SetPlainText(app.BacklightStatus());
        backlightSlider->SetValue(std::max(0, s.Backlight));
        UpdateRows();
    });
}

} // namespace bb
