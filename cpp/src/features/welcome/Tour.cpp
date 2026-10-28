#include "features/welcome/Tour.h"
#include "core/Lang.h"
#include "features/settings/SettingsHost.h"
#include "platform/Voice.h"
#include "ui/Access.h"
#include "ui/DirectWindow.h"
#include "ui/Theme.h"
#include "ui/controls/Basic.h"
#include "ui/controls/Pickers.h"
#include <cwchar>
#include <memory>

namespace bb::Tour {
namespace {

using namespace ui;

constexpr float W = 440, H = 430, Pad = 24;
constexpr UINT BuildMessage = WM_APP + 1;   // a step built afresh (never from inside one of its own buttons)
constexpr int Steps = 4;

// What each step says aloud (the same words as on the screen, in one breath).
struct Line { const wchar_t* key; const wchar_t* text; };
constexpr Line Spoken[Steps] = {
    {L"tour_welcome", L"Welcome to BlinkBuddy. Gentle breaks and a calmer screen while you work. This takes about a minute."},
    {L"tour_pick", L"Pick what helps you. You can change all of it later."},
    {L"tour_tray", L"BlinkBuddy waits in the tray by the clock. Click it for settings, right-click for quick actions like pausing."},
    {L"tour_done", L"You're all set. Want to see what a break looks like? Try one now, or finish the tour."},
};

// A rounded card behind a few controls (a feature's row, the language).
class Panel : public Container {
public:
    void Paint(Canvas& c) override { c.FillRound({0, 0, W(), H()}, 8, Theme::Card); }
};

// Which step this is: a dot for each, the current one drawn long in the accent.
class Dots : public Control {
public:
    int step = 0;
    void Paint(Canvas& c) override
    {
        float x = 0, cy = H() / 2;
        for (int i = 0; i < Steps; i++) {
            float w = i == step ? 16.0f : 6.0f;
            c.FillRound({x, cy - 3, x + w, cy + 3}, 3, i == step ? Theme::Accent() : Palette::Argb(85, 85, 85));
            x += w + 6;
        }
    }
    AccessInfo Access() const override { return {Role::Text, Lang::F(L"Step {0} of {1}", {std::to_wstring(step + 1), std::to_wstring(Steps)})}; }
};

// The tray by the clock with BlinkBuddy's icon, an arrow, and mini mode's round icon with its focus circle.
class TrayPicture : public Control {
public:
    void Paint(Canvas& c) override
    {
        c.FillRound({0, 0, W(), H()}, 8, Palette::Argb(21, 21, 21));
        float cy = H() / 2;
        D2D1_RECT_F bar{W() / 2 - 150, cy - 18, W() / 2 + 10, cy + 18};
        c.FillRound(bar, 6, Theme::Card);
        IDWriteTextFormat* glyphs = Fonts::Glyphs(11);
        c.Glyph(L'\uE701', glyphs, {bar.left + 8, cy - 12, bar.left + 32, cy + 12}, Theme::Dim);   // wi-fi
        c.Glyph(L'\uE767', glyphs, {bar.left + 34, cy - 12, bar.left + 58, cy + 12}, Theme::Dim);  // volume
        float ix = bar.left + 74;
        c.OutlineCircle(ix, cy, 11, Theme::Accent(), 2);
        c.Glyph(L'\uE890', Fonts::Glyphs(8), {ix - 9, cy - 9, ix + 9, cy + 9}, Theme::Accent());
        c.Text(L"14:05", Fonts::Small(), {ix + 18, cy - 10, bar.right - 4, cy + 10}, Theme::Dim);
        c.Glyph(L'\uE72A', glyphs, {bar.right + 10, cy - 12, bar.right + 40, cy + 12}, Palette::Argb(110, 110, 110));   // arrow
        float mx = bar.right + 80;
        c.FillCircle(mx, cy, 24, Theme::Card);
        c.OutlineCircle(mx, cy, 24, Palette::Argb(95, 208, 138), 3);
        c.Glyph(L'\uE890', Fonts::Glyphs(12), {mx - 14, cy - 14, mx + 14, cy + 14}, Theme::Fg);
    }
};

class TourWindow : public DirectWindow {
public:
    explicit TourWindow(SettingsHost& app) : app(app)
    {
        const Settings& s = app.S();
        bool fresh = !s.TourDone;
        // A first start suggests the everyday set; later the switches show what is on now.
        lookAway = fresh || s.BreakEnabled;
        night = fresh || (s.ScheduleEnabled && s.FilterEnabled);
        standUp = fresh || s.LongBreakEnabled;
        blink = !fresh && s.BlinkEnabled;
        quiet = std::wcsstr(GetCommandLineW(), L"--offscreen") != nullptr;   // (a diagnostic copy never speaks)
    }
    ~TourWindow() override { current = nullptr; }

    static inline TourWindow* current = nullptr;

    void Build()
    {
        Root().Clear();
        dots = nullptr;
        switch (step) {
        case 0: Welcome(); break;
        case 1: Pick(); break;
        case 2: Where(); break;
        default: Done(); break;
        }
        Relayout();
        if (primary) primary->Focus();
        Say();
        Access::Announce(Handle(), Lang::T(Spoken[step].text));
    }

protected:
    LRESULT OnMessage(UINT message, WPARAM w, LPARAM l) override
    {
        if (message == BuildMessage) {
            Build();
            return 0;
        }
        if (message == WM_CLOSE) Finish();   // closed with its X: seen, all the same
        return DirectWindow::OnMessage(message, w, l);
    }

    bool Shortcut(UINT key) override
    {
        if (key == VK_ESCAPE) Close();
        else if (key == VK_RETURN && primary) primary->Click();
        else return false;
        return true;
    }

private:
    Label& Text(std::wstring_view english, IDWriteTextFormat* font, int color, float x, float y, float w, float h, Container* in = nullptr)
    {
        auto& l = (in ? *in : Root()).Add<Label>(english, font, color);
        l.SetBounds(x, y, w, h);
        return l;
    }
    Label& Para(const std::wstring& text, float y, float h)
    {
        auto& l = Text(L"", Fonts::Body(), Theme::Dim, Pad, y, W - 2 * Pad, h);
        l.SetPlainText(text);
        l.wrap = l.top = true;
        return l;
    }
    void Title(std::wstring_view english, float y) { Text(english, Fonts::Get(15, gfx::Weight::Semibold), Theme::Fg, Pad, y, W - 2 * Pad, 30); }

    // The dots, and the buttons along the bottom: [back] and the step's own (Start, Next, Finish), in the accent.
    void Footer(std::wstring_view back, std::wstring_view next)
    {
        dots = &Root().Add<Dots>();
        dots->step = step;
        dots->SetBounds(Pad, H - 46, 70, 20);
        float x = W - Pad;
        auto& go = Root().Add<Button>(next);
        float gw = std::max(96.0f, TextWidth(Lang::T(next), Fonts::Body()) + 36);
        go.SetBounds(x - gw, H - 54, gw, 34);
        go.SetTint(Theme::Accent());
        x -= gw + 8;
        primary = &go;
        if (!back.empty()) {
            auto& b = Root().Add<Button>(back);
            float bw = std::max(80.0f, TextWidth(Lang::T(back), Fonts::Body()) + 36);
            b.SetBounds(x - bw, H - 54, bw, 34);
            backButton = &b;
        } else {
            backButton = nullptr;
        }
    }

    void Welcome()
    {
        auto& eye = Root().Add<Icon>(L'\uE890', 20.0f, Theme::Accent());
        eye.SetBounds(Pad - 4, 20, 40, 40);
        auto& sound = Root().Add<Button>(L"Read aloud");
        float sw = TextWidth(Lang::T(L"Read aloud"), Fonts::Body()) + 58;
        sound.SetBounds(W - Pad - sw, 22, sw, 34);
        sound.icon = [this](Canvas& c, const D2D1_RECT_F& r) { c.Glyph(readAloud ? L'\uE767' : L'\uE74F', Fonts::Glyphs(10), r, Theme::Fg); };
        sound.tip = Lang::T(readAloud ? L"On: each step is read aloud" : L"Off: nothing is read aloud");
        sound.onClick = [this, &sound] {
            readAloud = !readAloud;
            sound.tip = Lang::T(readAloud ? L"On: each step is read aloud" : L"Off: nothing is read aloud");
            sound.Invalidate();
            if (readAloud) Say();
            else if (voice) voice->Stop();
        };
        Title(L"Welcome to BlinkBuddy", 74);
        Para(Lang::T(L"Gentle breaks and a calmer screen while you work. This takes about a minute."), 110, 46);

        auto& card = Root().Add<Panel>();
        card.SetBounds(Pad, 170, W - 2 * Pad, 52);
        auto& globe = card.Add<Icon>(L'\uE774', 11.0f, Theme::Dim);
        globe.SetBounds(10, 14, 24, 24);
        Text(L"Language", Fonts::Body(), Theme::Fg, 44, 13, 150, 26, &card);
        std::vector<std::wstring> names;
        int chosen = 0;
        for (const auto& l : Lang::All()) {
            if (Lang::Code() == l.code) chosen = static_cast<int>(names.size());
            names.push_back(l.name);
        }
        auto& pick = card.Add<DropDown>(std::vector<std::wstring>{});
        pick.SetPlainItems(names);
        pick.SetSelected(chosen);
        pick.SetBounds(card.W() - 200, 9, 190, 34);
        pick.onChange = [this](int i) {
            app.SetLanguage(Lang::All()[i].code);
            voice.reset();   // (one for the new language)
            PostMessageW(Handle(), BuildMessage, 0, 0);
        };
        auto& note = Text(L"Each step is read aloud. The button above mutes it.", Fonts::Small(), Theme::Accent(), Pad, 232, W - 2 * Pad, 40);
        note.wrap = note.top = true;

        Footer(L"", L"Start");
        auto& skip = Root().Add<Button>(L"Skip tour");
        float kw = TextWidth(Lang::T(L"Skip tour"), Fonts::Body()) + 36;
        skip.SetBounds(primary->Bounds().left - 8 - kw, H - 54, kw, 34);
        skip.onClick = [this] { Close(); };
        primary->onClick = [this] { Go(1); };
    }

    // A feature's row: its icon in its colour, its name with a line under it, and a switch.
    void Feature(float y, wchar_t glyph, int tint, std::wstring_view name, const std::wstring& hint, bool& value)
    {
        auto& card = Root().Add<Panel>();
        card.SetBounds(Pad, y, W - 2 * Pad, 54);
        auto& icon = card.Add<Icon>(glyph, 12.0f, tint);
        icon.SetBounds(10, 14, 26, 26);
        Text(name, Fonts::Body(), Theme::Fg, 46, 7, card.W() - 46 - 66, 22, &card);
        auto& h = Text(L"", Fonts::Small(), Theme::Dim, 46, 28, card.W() - 46 - 66, 18, &card);
        h.SetPlainText(hint);
        auto& sw = card.Add<Toggle>(value);
        sw.SetBounds(card.W() - 56, 16, 44, 22);
        sw.onChange = [&value](bool on) { value = on; };
    }

    void Pick()
    {
        const Settings& s = app.S();
        Title(L"Pick what helps you", 22);
        Para(Lang::T(L"You can change all of it later."), 56, 24);
        Feature(92, L'\uE909', Palette::ModeTints[5], L"Look-away breaks",
                Lang::F(L"{0} s every {1} min, with eye exercises", {std::to_wstring(s.BreakDuration), std::to_wstring(s.BreakInterval)}), lookAway);
        Feature(152, L'\uE708', Palette::ModeTints[6], L"Warmer screen at night", Lang::T(L"From sunset to sunrise where you are"), night);
        Feature(212, L'\uE805', Palette::ModeTints[2], L"Stand-up breaks",
                Lang::F(L"A few stretches every {0} min", {std::to_wstring(s.LongBreakInterval)}), standUp);
        Feature(272, L'\uE890', Palette::ModeTints[1], L"Blink reminders", Lang::T(L"A small eye blinks with you"), blink);
        Footer(L"Back", L"Next");
        backButton->onClick = [this] { Go(0); };
        primary->onClick = [this] {
            Apply();
            Go(2);
        };
    }

    void Where()
    {
        Title(L"Where BlinkBuddy lives", 22);
        Para(Lang::T(L"It waits in the tray by the clock. Click it for settings, right-click for quick actions like pausing."), 56, 66);
        auto& picture = Root().Add<TrayPicture>();
        picture.SetBounds(Pad, 132, W - 2 * Pad, 110);
        auto& note = Text(L"Mini mode: a small icon that stays on top and shows the next break.", Fonts::Small(), Theme::Dim, Pad, 254, W - 2 * Pad, 40);
        note.wrap = note.top = true;
        Footer(L"Back", L"Next");
        backButton->onClick = [this] { Go(1); };
        primary->onClick = [this] { Go(3); };
    }

    void Done()
    {
        auto& check = Root().Add<Icon>(L'\uE930', 20.0f, Theme::Good);
        check.SetBounds(Pad - 4, 20, 40, 40);
        Title(L"You're all set", 70);
        const Settings& s = app.S();
        Para(s.BreakEnabled ? Lang::F(L"Your first break comes in {0} min. Want to see what one looks like?", {std::to_wstring(s.BreakInterval)})
                            : Lang::T(L"Want to see what a break looks like?"),
             106, 46);
        auto& tryIt = Root().Add<Button>(L"Try a break now");
        tryIt.SetBounds(Pad, 166, W - 2 * Pad, 40);
        tryIt.icon = [](Canvas& c, const D2D1_RECT_F& r) { c.Glyph(L'\uE768', Fonts::Glyphs(10), r, Theme::Accent()); };
        tryIt.onClick = [this] {
            Finish();
            SettingsHost& host = app;
            Close();
            host.StartBreak(false);
        };
        auto& note = Text(L"You can take this tour again from Options, under Help and support.", Fonts::Small(), Theme::Dim, Pad, 220, W - 2 * Pad, 40);
        note.wrap = note.top = true;
        Footer(L"Back", L"Finish");
        backButton->onClick = [this] { Go(2); };
        primary->onClick = [this] { Close(); };
    }

    void Go(int to)
    {
        step = to;
        PostMessageW(Handle(), BuildMessage, 0, 0);
    }

    // The features chosen on the second step, put in place.
    void Apply()
    {
        Settings& s = app.S();
        s.BreakEnabled = lookAway;
        if (night) {
            s.FilterEnabled = true;
            s.ScheduleEnabled = true;
            s.ScheduleMode = 0;   // sunset to sunrise
        } else {
            s.ScheduleEnabled = false;
        }
        s.LongBreakEnabled = standUp;
        s.BlinkEnabled = blink;
        app.Changed(true);
        app.Rebuild(false);   // (an open settings window shows them)
    }

    void Finish()
    {
        if (app.S().TourDone) return;
        app.S().TourDone = true;
        app.Changed();
    }

    void Say()
    {
        if (!readAloud || quiet) return;
        if (!voice) voice = std::make_unique<Voice>(Lang::Code());
        voice->Say(Spoken[step].key, Lang::T(Spoken[step].text));
    }

    SettingsHost& app;
    bool lookAway, night, standUp, blink, quiet = false, readAloud = true;
    int step = 0;
    Dots* dots = nullptr;
    Button* primary = nullptr;
    Button* backButton = nullptr;
    std::unique_ptr<Voice> voice;
};

} // namespace

void Open(SettingsHost& app)
{
    if (TourWindow::current) {
        SetForegroundWindow(TourWindow::current->Handle());
        return;
    }
    auto* tour = new TourWindow(app);   // (it deletes itself when it closes)
    TourWindow::current = tour;
    tour->onClosed = [tour] { DeleteLater(tour); };
    DirectWindow::Options options;
    options.dialog = true;
    options.title = L"BlinkBuddy";
    tour->Open(W, H, nullptr, options);
    tour->Build();
    tour->Show();
}

} // namespace bb::Tour
