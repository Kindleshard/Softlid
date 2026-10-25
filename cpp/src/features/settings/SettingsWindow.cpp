#include "features/settings/SettingsWindow.h"
#include "app/App.h"
#include "core/Lang.h"
#include "core/Modes.h"
#include "features/settings/CloseDialog.h"
#include "ui/MiniArt.h"
#include "ui/Theme.h"
#include <algorithm>
#include <cmath>

namespace bb {
namespace {

using namespace ui;

constexpr UINT_PTR StatusTimer = 1;              // (WM_TIMER ids below 0x10000 can't be a control's address)
constexpr UINT LaterMessage = WM_APP + 20;

} // namespace

SettingsWindow::SettingsWindow(App& app, Settings& settings, bool unfoldAll) : app(app), s(settings), unfoldAll(unfoldAll)
{
    tabs = &Root().Add<TabStrip>(std::vector<std::wstring>{L"Screen", L"Blink", L"Schedule", L"Extras", L"\uE713"});
    tabs->SetBounds(8, 8, Edge - 44, 40);
    tabs->tip = Lang::T(L"The cog holds BlinkBuddy's own options");
    tabs->onChange = [this](int selected) {
        for (int i = 0; i < 5; i++) pages[i]->SetVisible(i == selected);
        UpdateRows();
    };
    // Mini mode's button by the title bar (while mini mode is on in Options).
    titleMini = &Root().Add<Button>();
    titleMini->SetBounds(Edge - 34, 12, 32, 32);
    titleMini->iconSize = 20;
    titleMini->icon = [](Canvas& c, const D2D1_RECT_F& r) { MiniArt::DrawSymbol(c.Target(), r); };
    titleMini->tip = Lang::T(L"Mini mode: shrink this window into a small icon that stays on top");
    titleMini->SetVisible(s.MiniEnabled);
    titleMini->onClick = [this] { this->app.EnterMiniMode(); };
    Root().Add<ui::Divider>().SetBounds(0, 52, W, 1);
    for (int i = 0; i < 5; i++) {
        pages[i] = &Root().Add<ScrollPage>();
        pages[i]->SetBounds(0, 54, W, H - 54);   // (the real height comes with the window)
        pages[i]->SetVisible(i == 0);
        stacks.push_back(std::make_unique<Stack>(*pages[i]));
    }
    BuildScreenPage(*stacks[0]);
    BuildBlinkPage(*stacks[1]);
    BuildSchedulePage(*stacks[2]);
    BuildExtrasPage(*stacks[3]);
    BuildOptionsPage(*stacks[4]);
}

SettingsWindow::~SettingsWindow()
{
    if (Handle()) KillTimer(Handle(), StatusTimer);
}

void SettingsWindow::Show(const POINT* at, bool optionsPage, HICON icon, bool activate)
{
    Options opts;
    opts.resizable = true;
    opts.minHeight = 360;
    opts.icon = icon;
    float height = s.SettingsHeight > 0 ? static_cast<float>(s.SettingsHeight) : H;   // as tall as it was last left
    Open(W, height, at, opts);
    ShowRules();   // again, now the icons can be made at the display's scale
    if (optionsPage) {
        tabs->SetSelected(4);
        tabs->onChange(4);
    }
    RefreshStatus();
    SetTimer(Handle(), StatusTimer, 1000, nullptr);
    DirectWindow::Show(activate);
}

POINT SettingsWindow::Position() const
{
    RECT r{};
    GetWindowRect(Handle(), &r);
    return {r.left, r.top};
}

void SettingsWindow::Layout()
{
    D2D1_SIZE_F size = ClientSize();
    for (ScrollPage* page : pages) page->SetBounds(0, 54, W, std::max(1.0f, size.height - 54));
    UpdateRows();
}

void SettingsWindow::UpdateRows()
{
    for (auto& update : updaters) update();
    for (auto& st : stacks) st->Layout();
}

void SettingsWindow::Later(std::function<void()> work)
{
    later.push_back(std::move(work));
    PostMessageW(Handle(), LaterMessage, 0, 0);
}

void SettingsWindow::RefreshStatus()
{
    breakStatus->SetPlainText(app.BreakStatus());
    longStatus->SetPlainText(app.StandUpStatus());
    nightStatus->SetPlainText(app.NightStatus());
    ShowSync();
    // A rule's mode in use.
    if (app.RuleMode() >= 0) ruleStatus->SetPlainText(Lang::F(L"{0} mode, for the app in front", {Lang::T(Modes::Names[app.RuleMode()])}));
    if (app.RuleMode() != shownRule) {
        shownRule = app.RuleMode();
        UpdateRows();
    }
    hwStatus->SetPlainText(app.BacklightStatus());
    RefreshTone();
    focusRing->Invalidate();
    focusButton->SetText(app.FocusState() == App::Focus::Working ? L"Pause" : app.FocusState() == App::Focus::Paused ? L"Resume" : L"Start");
}

void SettingsWindow::SyncFromApp()
{
    filterToggle->SetOn(s.FilterEnabled);
    toneToggle->SetOn(s.ToneEnabled);
    lutBox->SetText(s.LutPath);
    brightSlider->SetValue(s.Brightness);
    coolToggle->SetOn(s.CoolRange);
    blueSlider->SetRange(s.CoolRange ? -75 : 0, 100);
    blueSlider->SetValue(s.BlueFilter);
    stripToggle->SetOn(s.StripEnabled);
    modeGrid->SetSelected(s.Mode);
    effectPicker->SetSelected(s.ColorEffect);
    awakeToggle->SetOn(app.KeepingAwake());
    UpdateRows();
}

// The close button (or Alt+F4): ask, keep BlinkBuddy running in the tray, or exit, as set in Options.
void SettingsWindow::CloseClicked()
{
    int choice = s.CloseAction;
    if (choice == 1) Close();
    else if (choice == 2) app.Quit();
    else CloseDialog::Ask(Handle(), [this](int answer, bool remember) {   // 0 cancelled, 1 tray, 2 exit
        if (answer == 0) return;
        if (remember) {
            s.CloseAction = answer;
            app.Changed();
        }
        if (answer == 2) app.Quit();
        else Close();
    });
}

LRESULT SettingsWindow::OnMessage(UINT message, WPARAM w, LPARAM l)
{
    switch (message) {
    case WM_TIMER:
        if (w == StatusTimer) {
            RefreshStatus();
            return 0;
        }
        break;
    case LaterMessage: {
        auto work = std::move(later);
        later.clear();
        for (auto& f : work) f();
        return 0;
    }
    case LutInstallMessage: {
        std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(l));
        LutInstallProgress(static_cast<int>(w), *text);
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((w & 0xFFF0) == SC_CLOSE) {
            CloseClicked();
            return 0;
        }
        // Minimising folds the window into mini mode (when mini mode is on) instead of the taskbar.
        if ((w & 0xFFF0) == SC_MINIMIZE && s.MiniEnabled) {
            app.EnterMiniMode();
            return 0;
        }
        break;
    case WM_NCHITTEST: {
        // Taller or shorter only: the side edges and corners resize up and down.
        LRESULT hit = DirectWindow::OnMessage(message, w, l);
        if (hit == HTLEFT || hit == HTRIGHT) return HTBORDER;
        if (hit == HTTOPLEFT || hit == HTTOPRIGHT) return HTTOP;
        if (hit == HTBOTTOMLEFT || hit == HTBOTTOMRIGHT) return HTBOTTOM;
        return hit;
    }
    case WM_EXITSIZEMOVE: {
        // The height is remembered (in 100%-scale units) once resizing ends.
        int height = static_cast<int>(std::lround(ClientSize().height));
        if (std::abs(height - (s.SettingsHeight > 0 ? s.SettingsHeight : static_cast<int>(H))) >= 2) {
            s.SettingsHeight = height;
            app.Changed();
        }
        break;
    }
    }
    return DirectWindow::OnMessage(message, w, l);
}

// ---- Row builders ----

Label& SettingsWindow::AddLabel(Container& parent, std::wstring_view text, IDWriteTextFormat* font, int color, float x, float y, float w, float h)
{
    Label& l = parent.Add<Label>(text, font, color);
    l.SetBounds(x, y, w, h);
    return l;
}

// The icon makes each section easy to spot while scrolling.
void SettingsWindow::Title(Container& row, wchar_t icon, std::wstring_view title)
{
    row.Add<Icon>(icon, 14.0f, Theme::Accent()).SetBounds(20, 9, 32, 30);
    AddLabel(row, title, Fonts::Heading(), Theme::Fg, 56, 10, 300, 28);
}

// A section's heading: its icon, title and one-line hint, with a chevron that folds the section shut (clicking the
// title does too); the rows after it, up to the next divider, belong to it. Folded sections are remembered. The
// chevron sits just left of where a switch goes.
Row& SettingsWindow::SectionHead(Stack& st, wchar_t icon, std::wstring_view title, std::wstring_view hint, Shown shown)
{
    st.EndSection();
    bool two = LinesFor(hint, Fonts::Small(), Edge - 56) > 1;   // a longer translation gets a second line
    Row& r = st.Add(two ? 82.0f : 64.0f, shown);
    Title(r, icon, title);
    Label& h = AddLabel(r, hint, Fonts::Small(), Theme::Dim, 56, 38, Edge - 56 - (two ? 0 : 0), two ? 38.0f : 20.0f);
    if (two) {
        h.wrap = true;
        h.top = true;
    }
    std::wstring key(title);
    auto& fold = r.Add<FoldButton>(!IsFolded(key));
    fold.SetBounds(Edge - 84, 13, 28, 28);
    fold.tip = Lang::T(L"Fold or unfold this section");
    auto toggle = [this, key, &fold] {
        SetFolded(key, !IsFolded(key));
        fold.SetOpen(!IsFolded(key));
        UpdateRows();
    };
    fold.onClick = toggle;
    auto& area = r.Add<HotArea>();   // the title and hint fold it too
    area.SetBounds(0, 0, Edge - 90, r.H());
    area.onClick = toggle;
    st.Section([this, key] { return !IsFolded(key); });
    return r;
}

// A section's heading with its switch.
Toggle& SettingsWindow::Header(Stack& st, wchar_t icon, std::wstring_view title, std::wstring_view hint, bool value,
                               std::function<void(bool)> changed, Shown shown)
{
    Row& r = SectionHead(st, icon, title, hint, std::move(shown));
    return AddToggle(r, Edge - 44, 13, value, std::move(changed));
}

bool SettingsWindow::IsFolded(const std::wstring& key) const
{
    if (unfoldAll) return false;
    return (L"|" + s.Folded + L"|").find(L"|" + key + L"|") != std::wstring::npos;
}

void SettingsWindow::SetFolded(const std::wstring& key, bool folded)
{
    std::vector<std::wstring> keys;
    std::wstring_view all = s.Folded;
    while (!all.empty()) {
        size_t bar = all.find(L'|');
        std::wstring part(all.substr(0, bar));
        all = bar == std::wstring_view::npos ? std::wstring_view{} : all.substr(bar + 1);
        if (!part.empty() && part != key) keys.push_back(part);
    }
    if (folded) keys.push_back(key);
    std::wstring joined;
    for (auto& k : keys) joined += (joined.empty() ? L"" : L"|") + k;
    s.Folded = joined;
    app.Changed();
}

// "More options": a section's less-used settings, folded away until opened (they stay open or folded while BlinkBuddy
// runs). The Simple view has no "More options" at all: just the essentials.
SettingsWindow::Shown SettingsWindow::More(Stack& st, const std::wstring& key, Shown shown)
{
    static std::vector<std::wstring> unfolded;   // (for as long as BlinkBuddy runs)
    auto isOpen = [this, key] { return unfoldAll || std::find(unfolded.begin(), unfolded.end(), key) != unfolded.end(); };
    Shown visible = [this, shown] { return s.AdvancedView && (!shown || shown()); };
    Row& row = st.Add(36, visible);
    auto& d = row.Add<Disclosure>(isOpen());
    d.SetBounds(20, 5, 220, 26);
    d.onToggle = [this, key](bool open) {
        auto at = std::find(unfolded.begin(), unfolded.end(), key);
        if (open && at == unfolded.end()) unfolded.push_back(key);
        if (!open && at != unfolded.end()) unfolded.erase(at);
        UpdateRows();
    };
    return [isOpen, visible] { return isOpen() && visible(); };
}

// A line between sections (it ends the one above).
void SettingsWindow::Divider(Stack& st, Shown shown)
{
    st.EndSection();
    st.Add(21, std::move(shown)).Add<ui::Divider>().SetBounds(24, 10, Edge - 24, 1);
}

void SettingsWindow::Note(Stack& st, std::wstring_view text, int lines, Shown shown)
{
    lines = std::max(lines, LinesFor(text, Fonts::Small(), Edge - 24));   // as many as the translation needs
    Row& r = st.Add(lines * 18.0f + 8, std::move(shown));
    Label& l = AddLabel(r, text, Fonts::Small(), Theme::Dim, 24, 2, Edge - 24, lines * 18.0f + 4);
    l.wrap = true;
    l.top = true;
}

Toggle& SettingsWindow::ToggleRow(Stack& st, std::wstring_view text, bool value, std::function<void(bool)> changed, Shown shown)
{
    Row& r = st.Add(34, std::move(shown));
    AddLabel(r, text, Fonts::Body(), Theme::Fg, 24, 4, 360, 26);
    return AddToggle(r, Edge - 44, 6, value, std::move(changed));
}

Toggle& SettingsWindow::AddToggle(Container& parent, float x, float y, bool value, std::function<void(bool)> changed)
{
    Toggle& t = parent.Add<Toggle>(value);
    t.SetBounds(x, y, 44, 22);
    t.onChange = std::move(changed);
    return t;
}

Slider& SettingsWindow::SliderRow(Stack& st, std::wstring_view text, int min, int max, int value, int def,
                                  std::function<std::wstring(int)> format, std::function<void(int)> changed, Shown shown)
{
    Row& r = st.Add(62, std::move(shown));
    AddLabel(r, text, Fonts::Body(), Theme::Fg, 24, 4, 240, 22);
    Label& shownValue = AddLabel(r, L"", Fonts::Body(), Theme::Dim, 264, 4, 140, 22);
    shownValue.SetText(format(value));
    shownValue.align = HAlign::Right;
    Slider& slider = r.Add<Slider>(min, max, value);
    slider.SetBounds(16, 28, 396, 30);
    slider.onChange = [&shownValue, format, changed](int v) {
        shownValue.SetText(format(v));
        changed(v);
    };
    Button& reset = r.Add<Button>();   // a small round "undo" button
    reset.SetGlyph(L'\uE7A7');
    reset.SetBounds(Edge - 30, 29, 28, 28);
    reset.focusable = false;
    reset.tip = Lang::T(L"Reset to default");
    reset.onClick = [&slider, def] {
        slider.SetValue(def);
        if (slider.onChange) slider.onChange(slider.Value());
    };
    return slider;
}

Button& SettingsWindow::AddButton(Container& parent, std::wstring_view text, float x, float y, float w, float h)
{
    Button& b = parent.Add<Button>(text);
    b.SetBounds(x, y, w, h);
    return b;
}

// How many lines a text takes (once translated) at a width.
int SettingsWindow::LinesFor(std::wstring_view text, IDWriteTextFormat* font, float width)
{
    float line = LineHeight(font), h = TextHeight(Lang::T(text), font, width);
    return std::max(1, static_cast<int>(std::ceil(h / line - 0.1)));
}

} // namespace bb
