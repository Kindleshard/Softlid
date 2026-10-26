// The panel: every feature switched on, as a card, in the order you drag them into; and their list in Options, where
// each is switched on (it runs, and its card shows) or off (it stops, and its card goes).
#include "features/settings/SettingsWindow.h"
#include "features/settings/SettingsHost.h"
#include "core/ColorMath.h"
#include "core/Lang.h"
#include "core/Modes.h"
#include "core/ToneCurve.h"
#include "ui/MiniArt.h"
#include "ui/Theme.h"
#include <algorithm>

namespace bb {

using namespace ui;

namespace {

struct FeatureInfo {
    const wchar_t* key;    // (in the saved order)
    const wchar_t* name;
    wchar_t icon;          // Segoe Fluent Icons
};

const FeatureInfo Features[] = {
    {L"filter", L"Screen filter", static_cast<wchar_t>(0xE706)},
    {L"tone", L"Tone", static_cast<wchar_t>(0xE793)},
    {L"blink", L"Blink reminder", static_cast<wchar_t>(0xE890)},
    {L"lookaway", L"Look-away reminder", static_cast<wchar_t>(0xE909)},
    {L"standup", L"Stand-up reminder", static_cast<wchar_t>(0xE805)},
    {L"night", L"Night schedule", static_cast<wchar_t>(0xE708)},
    {L"strip", L"Reading strip", static_cast<wchar_t>(0xE736)},
    {L"grain", L"Film grain", static_cast<wchar_t>(0xE81E)},
    {L"focus", L"Focus timer", static_cast<wchar_t>(0xE916)},
    {L"rules", L"Modes for apps", static_cast<wchar_t>(0xE7F4)},
};

constexpr float TopBar = 54;   // the bar with the cog, above the pages

} // namespace

// ---- The bar by the title: the page's name, the cog (Options), and mini mode's button ----

void SettingsWindow::BuildTopBar()
{
    backButton = &Root().Add<Button>();
    backButton->SetGlyph(static_cast<wchar_t>(0xE72B));
    backButton->SetBounds(12, 11, 32, 32);
    backButton->tip = Lang::T(L"Back to the panel");
    backButton->onClick = [this] { ShowOptions(false); };
    backButton->SetVisible(false);   // (in Options only)
    pageTitle = &AddLabel(Root(), L"Features", Fonts::Heading(), Theme::Fg, 20, 12, 260, 30);
    cogButton = &Root().Add<Button>();
    cogButton->SetGlyph(static_cast<wchar_t>(0xE713));
    cogButton->SetBounds(Edge - 72, 11, 32, 32);
    cogButton->tip = Lang::T(L"Options: which features are on, and how BlinkBuddy itself behaves");
    cogButton->onClick = [this] { ShowOptions(!optionsShown); };
    // Mini mode's button (while mini mode is on in Options).
    titleMini = &Root().Add<Button>();
    titleMini->SetBounds(Edge - 34, 11, 32, 32);
    titleMini->iconSize = 20;
    titleMini->icon = [](Canvas& c, const D2D1_RECT_F& r) { MiniArt::DrawSymbol(c.Target(), r); };
    titleMini->tip = Lang::T(L"Mini mode: shrink this window into a small icon that stays on top");
    titleMini->SetVisible(s.MiniEnabled);
    titleMini->onClick = [this] { this->app.EnterMiniMode(); };
    Root().Add<ui::Divider>().SetBounds(0, TopBar - 2, W, 1);
}

void SettingsWindow::ShowOptions(bool on)
{
    optionsShown = on;
    pages[0]->SetVisible(!on);
    pages[1]->SetVisible(on);
    backButton->SetVisible(on);
    pageTitle->SetText(on ? L"Options" : L"Features");
    pageTitle->SetBounds(on ? 52.0f : 20.0f, 12, 260, 30);
    cogButton->SetTint(on ? Theme::Accent() : 0);
    UpdateRows();
}

// ---- The features: on or off, and where they go ----

bool SettingsWindow::FeatureOn(int f) const
{
    switch (f) {
    case FFilter: return s.FilterEnabled;
    case FTone: return s.ToneEnabled;
    case FBlink: return s.BlinkEnabled;
    case FLookAway: return s.BreakEnabled;
    case FStandUp: return s.LongBreakEnabled;
    case FNight: return s.ScheduleEnabled || s.NightLightSync;   // (following Windows Night light runs the night too)
    case FStrip: return s.StripEnabled;
    case FGrain: return s.GrainEnabled;
    case FFocus: return s.FocusEnabled;
    case FRules: return s.RulesEnabled;
    }
    return false;
}

bool SettingsWindow::FeatureListed(int f) const { return (f != FGrain && f != FRules) || s.AdvancedView; }

void SettingsWindow::SetFeature(int f, bool on)
{
    switch (f) {
    case FFilter: SetFilter(on); break;
    case FTone: SetTone(on); break;   // (on, it switches the screen filter on too: tone works on top of it)
    case FBlink: s.BlinkEnabled = on; app.Changed(); break;
    case FLookAway: s.BreakEnabled = on; app.Changed(); break;
    case FStandUp: s.LongBreakEnabled = on; app.Changed(); break;
    case FNight:
        s.ScheduleEnabled = on;
        if (!on) s.NightLightSync = false;
        NightMarker();
        NightChanged();
        break;
    case FStrip: s.StripEnabled = on; app.Changed(true); break;
    case FGrain: s.GrainEnabled = on; app.Changed(true); break;
    case FFocus:
        s.FocusEnabled = on;
        if (!on) app.FocusStop();
        app.Changed();
        break;
    case FRules: s.RulesEnabled = on; app.Changed(true); break;
    }
    SyncFeatures();
    UpdateRows();
}

void SettingsWindow::SyncFeatures()
{
    for (int f = 0; f < FeatureCount; f++)
        if (featureToggles[f]) featureToggles[f]->SetOn(FeatureOn(f));
}

// The saved order (keys by commas), with any feature it doesn't name added where it comes by default.
void SettingsWindow::LoadOrder()
{
    order.clear();
    std::wstring_view all = s.PanelOrder;
    while (!all.empty()) {
        size_t comma = all.find(L',');
        std::wstring_view key = all.substr(0, comma);
        all = comma == std::wstring_view::npos ? std::wstring_view{} : all.substr(comma + 1);
        for (int f = 0; f < FeatureCount; f++)
            if (key == Features[f].key && std::find(order.begin(), order.end(), f) == order.end()) order.push_back(f);
    }
    for (int f = 0; f < FeatureCount; f++)
        if (std::find(order.begin(), order.end(), f) == order.end()) order.push_back(f);
}

// ---- The panel ----

void SettingsWindow::BuildPanel(Stack& st)
{
    st.cards = true;
    st.gap = 8;
    using Builder = void (SettingsWindow::*)(Stack&);
    const Builder builders[FeatureCount] = {&SettingsWindow::BuildFilter, &SettingsWindow::BuildTone, &SettingsWindow::BuildBlink,
        &SettingsWindow::BuildLookAway, &SettingsWindow::BuildStandUp, &SettingsWindow::BuildNight, &SettingsWindow::BuildStrip,
        &SettingsWindow::BuildGrain, &SettingsWindow::BuildFocusTimer, &SettingsWindow::BuildRules};
    for (int f = 0; f < FeatureCount; f++) {
        // (a diagnostic copy shows every card: its features stay off, so it never changes the screen it runs beside)
        st.BeginBlock(f, [this, f] { return (FeatureOn(f) || unfoldAll) && FeatureListed(f); });
        (this->*builders[f])(st);
        st.Add(8);   // (the card's own bottom margin)
        st.EndBlock();
    }
    st.SetOrder(order);

    // What is off, and where to switch it on.
    Row& offRow = st.Add(76, [this] {
        for (int f = 0; f < FeatureCount; f++)
            if (FeatureListed(f) && !FeatureOn(f)) return true;
        return false;
    });
    Label& off = AddLabel(offRow, L"", Fonts::Small(), Theme::Dim, 24, 4, Edge - 24, 38);
    off.align = HAlign::Center;
    off.wrap = off.top = true;
    Button& open = AddButton(offRow, L"Switch them on in Options", (W - 220) / 2, 44, 220, 28);
    open.onClick = [this] { ShowOptions(true); };
    updaters.push_back([this, &off] {
        std::wstring names;
        int count = 0, shown = 0;
        for (int f : order) {
            if (!FeatureListed(f)) continue;
            shown++;
            if (FeatureOn(f)) continue;
            names += (count++ ? L", " : L"") + Lang::T(Features[f].name);
        }
        off.SetPlainText(count == shown ? Lang::T(L"Every feature is off.") : Lang::F(L"Off: {0}", {names}));
    });
    st.Add(12);
}

// A feature's heading on the panel: a grip to drag it by, its icon and name, what it does now (its hint, a line on
// what it is for, is the tip), and the chevron that folds it.
void SettingsWindow::FeatureHead(Stack& st, Feature f, std::wstring_view hint)
{
    Row& r = SectionHead(st, Features[f].icon, Features[f].name, FeatureSummary(f), nullptr, 14, Edge - 36, &summaries[f]);
    summaries[f]->tip = Lang::T(hint);
    for (auto& child : r.Children())   // (the area that folds it lies over the name: it shows the tip there)
        if (auto* area = dynamic_cast<HotArea*>(child.get())) area->tip = Lang::T(hint);
    auto& grip = r.Add<DragGrip>();
    grip.SetBounds(14, 10, 18, 30);
    grip.tip = Lang::T(L"Drag to move it up or down");
    grip.onDrag = [this, f](float y) { DragFeature(f, 0, y); };
    grip.onDrop = [this, f](float y) { DropFeature(f, 0, y); };
}

// ---- The list in Options: each feature's switch, in the panel's order ----

void SettingsWindow::BuildFeatureList(Stack& st)
{
    SectionHead(st, static_cast<wchar_t>(0xE8FD), L"Features",
                L"Switched on, a feature runs and has its card on the panel. Drag them into the order you like.");
    for (int f = 0; f < FeatureCount; f++) {
        st.BeginBlock(f, [this, f] { return FeatureListed(f); });
        Row& r = st.Add(40);
        auto& grip = r.Add<DragGrip>();
        grip.SetBounds(18, 5, 18, 30);
        grip.tip = Lang::T(L"Drag to move it up or down");
        grip.onDrag = [this, f](float y) { DragFeature(f, 1, y); };
        grip.onDrop = [this, f](float y) { DropFeature(f, 1, y); };
        r.Add<Icon>(Features[f].icon, 12.0f, Theme::Accent()).SetBounds(42, 6, 28, 28);
        AddLabel(r, Features[f].name, Fonts::Body(), Theme::Fg, 78, 7, 260, 26);
        featureToggles[f] = &AddToggle(r, Edge - 44, 9, FeatureOn(f), [this, f](bool v) { SetFeature(f, v); });
        st.EndBlock();
    }
    st.SetOrder(order);
}

// ---- Dragging a feature into another place ----

int SettingsWindow::DropIndex(int page, float windowY) const
{
    auto blocks = stacks[page]->Blocks();
    float y = windowY - pages[page]->Origin().y + pages[page]->ScrollOffset().y;   // (in the page's content)
    for (size_t i = 0; i < blocks.size(); i++)
        if (y < (blocks[i].top + blocks[i].bottom) / 2) return static_cast<int>(i);
    return static_cast<int>(blocks.size());
}

void SettingsWindow::DragFeature(int, int page, float windowY)
{
    // Near the top or bottom edge, the page scrolls along.
    ScrollPage& p = *pages[page];
    float top = p.Origin().y, bottom = top + p.H();
    if (windowY < top + 36) p.ScrollBy(-14);
    else if (windowY > bottom - 36) p.ScrollBy(14);

    auto blocks = stacks[page]->Blocks();
    if (blocks.empty()) return;
    int i = DropIndex(page, windowY);
    float half = stacks[page]->gap / 2;
    float y = i < static_cast<int>(blocks.size()) ? blocks[i].top - half : blocks.back().bottom + half;
    marks[page]->SetBounds(16, y - 1.5f, W - 32, 3);
    marks[page]->SetVisible(true);
    marks[page]->Invalidate();
}

void SettingsWindow::DropFeature(int f, int page, float windowY)
{
    marks[page]->SetVisible(false);
    auto blocks = stacks[page]->Blocks();
    if (blocks.empty()) return;
    int i = DropIndex(page, windowY);
    // Before the feature it was dropped above, or just after the last one shown (dropped where it was: no change).
    int target = i < static_cast<int>(blocks.size()) ? blocks[i].id : -1, last = blocks.back().id;
    if (target == f || (target < 0 && last == f)) return;
    order.erase(std::find(order.begin(), order.end(), f));
    if (target >= 0) order.insert(std::find(order.begin(), order.end(), target), f);
    else order.insert(std::find(order.begin(), order.end(), last) + 1, f);
    std::wstring saved;
    for (int g : order) saved += (saved.empty() ? L"" : L",") + std::wstring(Features[g].key);
    s.PanelOrder = saved;
    app.Changed();
    for (auto& st : stacks) st->SetOrder(order);
    UpdateRows();
}

} // namespace bb

namespace bb {

using namespace ui;

namespace {

std::wstring Clock(int minutes)
{
    wchar_t text[8];
    swprintf_s(text, L"%02d:%02d", minutes / 60 % 24, minutes % 60);
    return text;
}

std::wstring Num(int v) { return std::to_wstring(v); }

} // namespace

std::wstring SettingsWindow::FeatureSummary(int f) const
{
    const std::wstring dot = L" \u00B7 ";
    switch (f) {
    case FFilter: {
        std::wstring mode = s.Mode >= 0 && s.Mode < Modes::Count ? Lang::T(Modes::Names[s.Mode]) : Lang::T(L"Custom");
        std::wstring blue = s.BlueFilter == 0 ? Lang::T(L"No blue light filter") : Num(ColorMath::KelvinFor(s.BlueFilter)) + L" K";
        return mode + dot + Num(s.Brightness) + L"%" + dot + blue;
    }
    case FTone: return s.ComfortLevel >= 0 && s.ComfortLevel < 4 ? Lang::T(ToneCurve::Levels[s.ComfortLevel]) : Lang::T(L"Custom");
    case FBlink: return Lang::F(L"Every {0} s", {Num(s.BlinkInterval)}) + dot + Lang::T(s.BlinkStyle == 1 ? L"Eyelids" : L"Eye icon");
    case FLookAway:
        return Lang::F(L"Every {0} min for {1} s", {Num(s.BreakInterval), Num(s.BreakDuration)}) +
               (s.Exercises ? dot + Lang::T(L"guided eye exercises") : L"");
    case FStandUp: return Lang::F(L"Every {0} min for {1} min", {Num(s.LongBreakInterval), Num(s.LongBreakDuration)});
    case FNight: {
        if (s.NightLightSync && app.FollowsNightLight()) return Lang::T(L"Follows Windows Night light");
        std::wstring when = s.ScheduleMode == 1 ? Lang::F(L"{0} to {1}", {Clock(s.NightFrom), Clock(s.NightTo)}) : Lang::T(L"Sunset to sunrise");
        return when + dot + Num(ColorMath::KelvinFor(s.NightBlue)) + L" K";
    }
    case FStrip: {
        static const wchar_t* const shapes[] = {L"Band", L"Column", L"Rectangle", L"Circle"};
        return Lang::T(shapes[std::clamp(s.StripShape, 0, 3)]) + dot + Lang::F(L"{0}% dimmed", {Num(s.StripDim)});
    }
    case FGrain: return Lang::F(L"{0}% strength", {Num(s.GrainStrength)});
    case FFocus: return Lang::F(L"{0} min work, {1} min rest", {Num(s.FocusWork), Num(s.FocusRest)});
    case FRules: {
        int count = 0;
        for (wchar_t c : s.AppRules) count += c == L'|';
        count += s.AppRules.empty() ? 0 : 1;
        return count == 0 ? Lang::T(L"No apps yet") : Lang::F(L"Apps with a mode: {0}", {Num(count)});
    }
    }
    return L"";
}

void SettingsWindow::RefreshSummaries()
{
    for (int f = 0; f < FeatureCount; f++)
        if (summaries[f]) summaries[f]->SetPlainText(FeatureSummary(f));
}

} // namespace bb
