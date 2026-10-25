// The Screen page: the screen filter, its modes and colour effects, and the backlight; tone, with its engines, is in
// SettingsTone.cpp.
#include "features/settings/SettingsWindow.h"
#include "app/App.h"
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

void SettingsWindow::BuildScreenPage(Stack& st)
{
    filterToggle = &Header(st, L'\uE706', L"Screen filter", L"Ctrl+Alt+Shift with F: on/off · ↑ ↓: brightness · ← →: warmth",
                           s.FilterEnabled, [this](bool v) { SetFilter(v); });
    // One-click modes; moving a slider makes it your own (Custom) again.
    Row& modeRow = st.Add(88);
    std::vector<std::wstring> glyphs, names;
    for (int i = 0; i < Modes::Count; i++) {
        glyphs.push_back(Modes::Glyphs[i]);
        names.push_back(Modes::Names[i]);
    }
    modeGrid = &modeRow.Add<ModeGrid>(glyphs, names);
    modeGrid->SetBounds(24, 4, Edge - 24, 80);
    modeGrid->SetSelected(s.Mode);
    modeGrid->onChange = [this](int mode) {
        app.ChooseMode(mode);
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
    NightMarker();
    // A colour effect and the blue light filter take turns: while an effect is on, the slider is greyed and says so
    // (moving it brings the blue light filter back and the effect goes back to Original).
    Row& pausedRow = st.Add(26, [this] { return EffectPausesBlue(); });
    Label& paused = AddLabel(pausedRow, L"", Fonts::Small(), Theme::Dim, 24, 0, Edge - 24, 22);
    updaters.push_back([this, &paused] {
        blueSlider->SetMuted(EffectPausesBlue());
        paused.SetPlainText(EffectPausedText());
    });

    // The real backlight: a switch, then a slider of its own; Brightness above always dims pixels.
    hwToggle = &ToggleRow(st, L"Dim with the display's real backlight", s.HardwareBrightness, [this](bool on) { BacklightToggled(on); });
    backlightSlider = &SliderRow(st, L"Display backlight", 0, 100, std::max(0, s.Backlight), 100, Percent, [this](int v) {
        s.Backlight = v;
        app.Changed(true);
    }, [this] { return s.HardwareBrightness; });
    Row& hwRow = st.Add(26, [this] { return s.HardwareBrightness || !app.BacklightStatus().empty(); });
    hwStatus = &AddLabel(hwRow, L"", Fonts::Small(), Theme::Dim, 24, 2, Edge - 24, 20);
    BuildEffects(st);

    Shown more = More(st, L"screen"), advanced = And(more, [this] { return s.AdvancedView; });
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

    Divider(st);
    BuildTone(st);
    st.Add(16);
}

// ---- Colour effects: each shown as a little picture through it ----

void SettingsWindow::BuildEffects(Stack& st)
{
    Row& row = st.Add(80);   // (with room above it, apart from the sliders)
    AddLabel(row, L"Color effect", Fonts::Body(), Theme::Fg, 24, 34, 124, 26);
    effectPicker = &row.Add<TilePicker>(5, s.ColorEffect);
    effectPicker->SetBounds(150, 22, 5 * 48 + 4 * 13 + 2, 48);
    effectPicker->painter = [this](Canvas& c, const D2D1_RECT_F& r, int i, float awake) { PaintEffect(c, r, i, awake); };
    effectPicker->onChange = [this](int effect) {
        s.ColorEffect = effect;
        app.ManualScreenChange();
        FilterTouched();
        UpdateRows();
    };
    effectPicker->onHover = [this](int i) { effectPicker->tip = i >= 0 ? Lang::T(EffectNames[i]) : L""; };
    Row& styleRow = st.Add(46, [this] { return s.AdvancedView && s.ColorEffect == ColorMath::Grayscale; });
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

// Whether a colour effect is taking the blue light filter's place right now.
bool SettingsWindow::EffectPausesBlue() const
{
    return s.FilterEnabled && s.ColorEffect != ColorMath::NoEffect && app.EffectsAvailable();
}

std::wstring SettingsWindow::EffectPausedText() const
{
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
    filterToggle->SetOn(on);
    app.ManualScreenChange();
    app.Changed(true);
    UpdateRows();
}

// Moving a filter control while the filter is off would do nothing visible, so switch it on.
void SettingsWindow::FilterTouched()
{
    if (!s.FilterEnabled) SetFilter(true);
    else app.Changed(true);
    modeGrid->SetSelected(s.Mode);
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
