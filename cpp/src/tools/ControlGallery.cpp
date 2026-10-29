#include "tools/ControlGallery.h"
#include "core/ColorMath.h"
#include "core/Lang.h"
#include "core/Modes.h"
#include "ui/DirectWindow.h"
#include "ui/EyeArt.h"
#include "ui/Layout.h"
#include "ui/Theme.h"
#include "ui/controls/Basic.h"
#include "ui/controls/Choices.h"
#include "ui/controls/ColorPicker.h"
#include "ui/controls/Pickers.h"
#include <cmath>

namespace bb {
namespace {

using namespace ui;

constexpr float W = 760, H = 620, LeftW = 380;

class Gallery : public DirectWindow {
public:
    Gallery()
    {
        auto& tabs = Root().Add<TabStrip>(std::vector<std::wstring>{L"Screen", L"Blink", L"Schedule", L"Extras", L"\uE713"});
        tabs.SetBounds(8, 8, 412, 40);
        tabs.tip = L"Tabs: words, or an icon";
        auto& mini = Root().Add<Button>();
        mini.SetGlyph(L'\uE73F');
        mini.SetBounds(W - 50, 12, 32, 32);
        mini.tip = L"A round icon button";
        Root().Add<Divider>().SetBounds(0, 52, W, 1);
        page = &Root().Add<ScrollPage>();
        stack = std::make_unique<Stack>(*page);
        BuildLeft();
        BuildRight();
    }

protected:
    void Layout() override
    {
        D2D1_SIZE_F size = ClientSize();
        page->SetBounds(0, 54, LeftW, size.height - 54);
        stack->Layout();
    }

private:
    // A row: a label on the left and a control on the right (the settings' usual row).
    template <class T, class... Args>
    T& LabelRow(std::wstring_view text, float controlW, float controlH, Args&&... args)
    {
        Row& r = stack->Add(44);
        r.Add<Label>(text).SetBounds(16, 0, LeftW - controlW - 40, 44);
        T& control = r.Add<T>(std::forward<Args>(args)...);
        control.SetBounds(LeftW - controlW - 20, (44 - controlH) / 2, controlW, controlH);
        return control;
    }

    void BuildLeft()
    {
        Row& head = stack->Add(48);
        head.Add<Label>(L"Screen filter", Fonts::Heading()).SetBounds(16, 8, 250, 40);
        auto& fold = head.Add<FoldButton>(sectionOpen);
        fold.SetBounds(LeftW - 52, 14, 28, 28);
        fold.onClick = [this, &fold] {
            sectionOpen = !sectionOpen;
            fold.SetOpen(sectionOpen);
            stack->Layout();
        };
        stack->Section([this] { return sectionOpen; });

        LabelRow<Toggle>(L"Screen filter", 44, 22, true).tip = L"A switch";
        LabelRow<Slider>(L"Brightness", 180, 30, 20, 100, 80);
        auto& warm = LabelRow<Slider>(L"Blue light filter", 180, 30, 0, 100, 45);
        warm.warm = true;
        warm.SetMarker(66);
        LabelRow<Slider>(L"Muted", 180, 30, 0, 100, 30).SetMuted(true);
        stack->EndSection();

        Row& steps = stack->Add(56);
        steps.Add<StepSlider>(std::vector<std::wstring>{L"Off", L"Gentle", L"Strong", L"Max"}).SetBounds(16, 6, LeftW - 36, 48);
        steps.Children().back()->tip = L"Steps";
        static_cast<StepSlider&>(*steps.Children().back()).SetSelected(1);

        Row& seg = stack->Add(48);
        seg.Add<Segmented>(std::vector<std::wstring>{L"Sunset to sunrise", L"Custom hours"}).SetBounds(16, 8, LeftW - 36, 32);

        auto& languages = LabelRow<DropDown>(L"Language", 170, 32, std::vector<std::wstring>{});
        std::vector<std::wstring> names;
        for (const auto& l : Lang::All()) names.push_back(l.name);
        languages.SetPlainItems(names);

        Row& more = stack->Add(30);
        more.Add<Disclosure>(moreOpen).SetBounds(16, 4, 200, 22);
        static_cast<Disclosure&>(*more.Children().back()).onToggle = [this](bool open) {
            moreOpen = open;
            stack->Layout();
        };
        Row& extra = stack->Add(44, [this] { return moreOpen; });
        extra.Add<Label>(L"Shown with \"More options\"", Fonts::Small(), Theme::Dim).SetBounds(16, 0, LeftW - 32, 44);

        Row& choices = stack->Add(124);
        auto& a = choices.Add<ChoiceRow>(L"Standard", L"Works everywhere, nothing to install", L"Recommended");
        a.SetBounds(16, 4, LeftW - 36, 56);
        a.SetChecked(true);
        auto& b = choices.Add<ChoiceRow>(L"Smart (GPU)", L"Adapts to what is on the screen");
        b.SetBounds(16, 64, LeftW - 36, 56);
        a.onChosen = [&a, &b] { a.SetChecked(true); b.SetChecked(false); };
        b.onChosen = [&a, &b] { b.SetChecked(true); a.SetChecked(false); };

        Row& steppers = stack->Add(64);
        steppers.Add<Stepper>(L"Every", 5, 120, 5, 20, [](int v) { return std::to_wstring(v) + L" min"; }).SetBounds(16, 4, 150, 56);
        steppers.Add<Stepper>(L"For", 10, 60, 5, 20, [](int v) { return std::to_wstring(v) + L" s"; }).SetBounds(186, 4, 150, 56);

        auto& city = LabelRow<TextBox>(L"City", 170, 30, 40);
        city.SetText(L"Istanbul");

        Row& progress = stack->Add(28);
        progress.Add<ProgressLine>().SetBounds(16, 11, LeftW - 36, 6);
        static_cast<ProgressLine&>(*progress.Children().back()).SetProgress(0.6f);

        auto& off = LabelRow<Toggle>(L"Switched off (disabled)", 44, 22, true);
        off.SetEnabled(false);
        auto& offButton = LabelRow<Button>(L"Disabled button", 130, 32, L"Can't click");
        offButton.SetEnabled(false);
        LabelRow<Button>(L"Button", 130, 32, L"Take a break");
        Row& wrapped = stack->Add(64);
        auto& note = wrapped.Add<Label>(L"A longer note wraps onto more lines when it doesn't fit on one, like the hints under the "
                                        L"settings do.", Fonts::Small(), Theme::Dim);
        note.wrap = true;
        note.SetBounds(16, 4, LeftW - 36, 56);
    }

    void BuildRight()
    {
        constexpr float x = LeftW + 12, w = W - LeftW - 32;
        Root().Add<Label>(L"Screen mode", Fonts::Heading()).SetBounds(x, 60, w, 28);
        std::vector<std::wstring> glyphs, names;
        for (int i = 0; i < Modes::Count; i++) {
            glyphs.push_back(Modes::Glyphs[i]);
            names.push_back(Modes::Names[i]);
        }
        auto& modes = Root().Add<ModeGrid>(glyphs, names);
        modes.SetBounds(x, 92, w, 76);
        modes.SetSelected(Modes::Health);

        Root().Add<Label>(L"Color effect", Fonts::Heading()).SetBounds(x, 180, w, 28);
        auto& effects = Root().Add<TilePicker>(5, 0);
        effects.SetBounds(x, 212, w, 50);
        effects.painter = [](Canvas& c, const D2D1_RECT_F& r, int i, float awake) {
            // A little landscape through each effect: sky, sun and grass.
            int sky = Palette::Argb(90, 160, 230), sun = Palette::Argb(255, 200, 60), grass = Palette::Argb(80, 170, 90);
            auto through = [i](int argb) {
                int red = (argb >> 16) & 0xFF, green = (argb >> 8) & 0xFF, blue = argb & 0xFF;
                ColorMath::Through(i, 0, red, green, blue);
                return Palette::Argb(red, green, blue);
            };
            float k = 0.55f + 0.45f * awake;
            c.FillRound(r, 4, Mix(Theme::Bg, through(sky), k));
            c.Fill({r.left, r.top + Height(r) * 0.62f, r.right, r.bottom}, Mix(Theme::Bg, through(grass), k));
            c.FillCircle(r.left + Width(r) * 0.68f, r.top + Height(r) * 0.36f, Height(r) * 0.16f, Mix(Theme::Bg, through(sun), k));
        };

        Root().Add<Label>(L"Corner", Fonts::Heading()).SetBounds(x, 276, 120, 28);
        Root().Add<CornerPicker>(3).SetBounds(x, 308, 72, 48);
        Root().Add<Label>(L"Colour", Fonts::Heading()).SetBounds(x + 120, 276, 120, 28);
        auto& color = Root().Add<ColorButton>();
        color.SetBounds(x + 120, 308, 40, 40);
        color.SetValue(Palette::Accents[4]);
        color.allowAccent = true;
        color.tip = L"Opens the colour wheel";

        auto& tinted = Root().Add<Button>(L"Update");
        tinted.SetBounds(x, 372, 110, 32);
        tinted.SetTint(Theme::Warn);
        auto& withIcon = Root().Add<Button>(L"Mini mode");
        withIcon.SetBounds(x + 120, 372, 140, 32);
        withIcon.iconSize = 18;
        withIcon.icon = [](Canvas& c, const D2D1_RECT_F& r) {
            float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
            c.FillCircle(cx, cy, Width(r) / 2, Palette::Argb(34, 36, 42));
            EyeArt::Draw(c.Target(), Inset(r, 3, 5), 1, 0, 0, 1.2f);
        };

        auto& curve = Root().Add<CurveView>();
        curve.SetBounds(x, 420, 120, 120);
        curve.curve = [](double v) { return std::pow(v, 1.6) * 0.92; };
        auto& eye = Root().Add<EyeView>();
        eye.SetBounds(x + 140, 430, 140, 100);
        eye.Set(0.4f, -0.2f, 1);
    }

    ScrollPage* page = nullptr;
    std::unique_ptr<Stack> stack;
    bool sectionOpen = true, moreOpen = false;
};

} // namespace

int RunControlGallery(bool offscreen)
{
    auto gallery = std::make_unique<Gallery>();
    gallery->onClosed = [] { PostQuitMessage(0); };
    POINT away{-30000, 0};
    DirectWindow::Options options;
    options.title = L"Softlid controls";
    options.resizable = true;
    options.minHeight = 300;
    gallery->Open(W, H, offscreen ? &away : nullptr, options);
    gallery->Show(!offscreen);
    if (offscreen) SetTimer(gallery->Handle(), 0, 180000, [](HWND h, UINT, UINT_PTR, DWORD) { PostMessageW(h, WM_CLOSE, 0, 0); });
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    gallery->onClosed = nullptr;
    gallery.reset();
    Fonts::Release();
    return 0;
}

} // namespace bb
