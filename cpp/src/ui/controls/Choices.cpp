#include "ui/controls/Choices.h"
#include "core/Lang.h"
#include "ui/Theme.h"
#include <algorithm>
#include <cmath>

namespace bb::ui {
namespace {

std::vector<std::wstring> Translated(std::vector<std::wstring> english)
{
    for (auto& s : english) s = Lang::T(s);
    return english;
}

constexpr int TileFill = Palette::Argb(40, 40, 43), TileHover = Palette::Argb(46, 46, 50);
constexpr int ChipFill = Palette::Argb(50, 50, 54), ChipHover = Palette::Argb(60, 60, 65);

// a mixed k of the way towards b (both opaque)
int Blend(int a, int b, float k)
{
    auto ch = [&](int s) { return static_cast<int>(std::lround(((a >> s) & 0xFF) * (1 - k) + ((b >> s) & 0xFF) * k)); };
    return Palette::Argb(ch(16), ch(8), ch(0));
}

} // namespace

// ---- Choice ----

void Choice::SetSelected(int i)
{
    if (i == selected) return;
    selected = i;
    Invalidate();
}

void Choice::Pick(int i)
{
    if (i == selected) return;
    selected = i;
    Invalidate();
    if (onChange) onChange(i);
}

// ---- Segmented ----

Segmented::Segmented(std::vector<std::wstring> englishItems) : items(Translated(std::move(englishItems)))
{
    cursor = Cursor::Hand;
}

void Segmented::Paint(Canvas& c)
{
    D2D1_RECT_F all{0.5f, 0.5f, W() - 0.5f, H() - 0.5f};
    c.FillRound(all, H() * 0.2f, Palette::Argb(43, 43, 43));
    c.OutlineRound(all, H() * 0.2f, Theme::Line);
    float w = Width(all) / std::max<size_t>(1, items.size());
    for (size_t i = 0; i < items.size(); i++) {
        D2D1_RECT_F r{all.left + i * w, all.top, all.left + (i + 1) * w, all.bottom};
        bool on = static_cast<int>(i) == selected;
        if (on) c.FillRound(Inset(r, 3, 3), H() * 0.15f, Palette::Argb(70, 70, 70));
        c.Text(items[i], Fonts::Body(), r, on ? Theme::Fg : Theme::Dim, HAlign::Center);
    }
}

void Segmented::MouseDown(D2D1_POINT_2F p)
{
    int n = static_cast<int>(items.size());
    Pick(std::clamp(static_cast<int>(p.x * n / std::max(1.0f, W())), 0, n - 1));
}

// ---- TabStrip ----

TabStrip::TabStrip(std::vector<std::wstring> englishItems)
{
    for (auto& item : englishItems) items.push_back(IsGlyph(item) ? item : Lang::T(item));
    cursor = Cursor::Hand;
}

std::vector<D2D1_RECT_F> TabStrip::ItemBounds() const
{
    std::vector<D2D1_RECT_F> rects;
    float pad = H() / 4, x = 0;
    for (auto& item : items) {
        float w = TextWidth(item, IsGlyph(item) ? Fonts::Glyphs(10.5f) : Fonts::Get(10.5f)) + 2 * pad + 6;
        rects.push_back({x, 0, x + w, H()});
        x += w;
    }
    return rects;
}

void TabStrip::Paint(Canvas& c)
{
    auto rects = ItemBounds();
    for (size_t i = 0; i < items.size(); i++) {
        bool on = static_cast<int>(i) == selected;
        int ink = on ? Theme::Fg : Theme::Dim;
        if (IsGlyph(items[i])) c.Glyph(items[i][0], Fonts::Glyphs(10.5f), rects[i], ink);
        else c.Text(items[i], Fonts::Get(10.5f), rects[i], ink, HAlign::Center);
        if (!on) continue;
        float uw = H() * 0.4f, uh = std::max(2.0f, H() * 0.075f);
        D2D1_RECT_F u = Rect(rects[i].left + (Width(rects[i]) - uw) / 2, H() - uh - 1, uw, uh);
        c.FillRound(u, uh / 2, Theme::Accent());
    }
}

void TabStrip::MouseDown(D2D1_POINT_2F p)
{
    auto rects = ItemBounds();
    for (size_t i = 0; i < rects.size(); i++)
        if (Contains(rects[i], p)) Pick(static_cast<int>(i));
}

// ---- ChoiceRow ----

ChoiceRow::ChoiceRow(std::wstring_view title, std::wstring_view note, std::wstring_view tag)
    : title(Lang::T(title)), note(Lang::T(note)), tag(tag.empty() ? L"" : Lang::T(tag))
{
    cursor = Cursor::Hand;
}

void ChoiceRow::SetChecked(bool on)
{
    if (on == checked) return;
    checked = on;
    Invalidate();
}

void ChoiceRow::Paint(Canvas& c)
{
    float u = LineHeight(Fonts::Body()), noteH = LineHeight(Fonts::Small());
    int accent = Theme::Accent();
    if (checked) {
        D2D1_RECT_F r{0.5f, 0.5f, W() - 0.5f, H() - 0.5f};
        c.FillRound(r, u * 0.35f, Palette::Argb(43, 43, 43));
        c.OutlineRound(r, u * 0.35f, accent, 1, 150 / 255.0f);
    }
    // Title and note centred as a block, with even room above and below inside the outline.
    float block = u * 1.1f + noteH, top = (H() - block) / 2;
    float cx = u * 0.95f, cy = top + u / 2, r = u * 0.36f;
    c.OutlineCircle(cx, cy, r, checked ? accent : Theme::Dim, std::max(1.5f, u / 12));
    if (checked) c.FillCircle(cx, cy, r * 0.5f, accent);

    float x = u * 1.8f;
    c.Text(title, Fonts::Body(), {x, cy - u / 2, W(), cy + u / 2}, Theme::Fg);
    if (!tag.empty()) {
        float tw = TextWidth(title, Fonts::Body()), gw = TextWidth(tag, Fonts::Small());
        D2D1_RECT_F pill = Rect(x + tw + u * 0.5f, cy - noteH / 2 - u * 0.1f, gw + u * 0.6f, noteH + u * 0.2f);
        c.FillRound(pill, Height(pill) * 0.3f, Palette::Argb(66, 52, 30));
        c.Text(tag, Fonts::Small(), pill, Palette::Argb(240, 190, 100), HAlign::Center);
    }
    c.Text(note, Fonts::Small(), Rect(x, top + u * 1.1f, W() - x - u * 0.5f, noteH + 2), Theme::Dim);
}

void ChoiceRow::MouseDown(D2D1_POINT_2F)
{
    if (onChosen) onChosen();
}

// ---- CornerPicker ----

CornerPicker::CornerPicker(int sel)
{
    selected = sel;
    cursor = Cursor::Hand;
}

D2D1_RECT_F CornerPicker::Frame() const { return {1.5f, 1.5f, W() - 1.5f, H() - 1.5f}; }

D2D1_POINT_2F CornerPicker::Spot(int i) const
{
    D2D1_RECT_F f = Frame();
    if (i == 4) return {(f.left + f.right) / 2, (f.top + f.bottom) / 2};
    float m = Height(f) * 0.24f;
    return {i % 2 == 0 ? f.left + m : f.right - m, i < 2 ? f.top + m : f.bottom - m};
}

void CornerPicker::Paint(Canvas& c)
{
    c.FillRound(Frame(), H() * 0.1f, Palette::Argb(42, 42, 42));
    c.OutlineRound(Frame(), H() * 0.1f, Theme::Track, std::max(1.0f, H() / 60));
    for (int i = 0; i < 5; i++) {
        D2D1_POINT_2F s = Spot(i);
        bool on = i == selected;
        c.FillCircle(s.x, s.y, H() * (on ? 0.11f : 0.065f), on ? Theme::Accent() : Theme::Track);
    }
}

void CornerPicker::MouseDown(D2D1_POINT_2F p)
{
    int best = 0;
    float bestD = 1e30f;
    for (int i = 0; i < 5; i++) {
        D2D1_POINT_2F s = Spot(i);
        float d = (s.x - p.x) * (s.x - p.x) + (s.y - p.y) * (s.y - p.y);
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    Pick(best);
}

// ---- ModeGrid ----

ModeGrid::ModeGrid(std::vector<std::wstring> glyphs, std::vector<std::wstring> englishNames)
    : glyphs(std::move(glyphs)), names(Translated(std::move(englishNames)))
{
    selected = -1;
    cursor = Cursor::Hand;
}

// Chips flow left to right, each as wide as its icon and name, onto as many rows as they need; the rows share the
// height (a chip at most 34 high).
D2D1_RECT_F ModeGrid::Tile(int i) const
{
    if (iconsOnly) {   // a group close together, centred with room either side; the names' line under them
        float d = std::min(38.0f, H() - 22), gap = 12, n = static_cast<float>(names.size());
        float left = std::max(0.0f, (W() - n * d - (n - 1) * gap) / 2);
        float x = left + i * (d + gap);
        return {x, 0, x + d, d};
    }
    constexpr float gap = 8, pad = 12, space = 7;
    IDWriteTextFormat* glyphFont = Fonts::Glyphs(11);
    std::vector<D2D1_RECT_F> chips;
    float x = 0;
    int row = 0;
    for (size_t k = 0; k < names.size(); k++) {
        float w = pad * 2 + TextWidth(glyphs[k], glyphFont) + space + TextWidth(names[k], Fonts::Small()) + 4;
        w = std::min(w, W());
        if (x > 0 && x + w > W()) {
            x = 0;
            row++;
        }
        chips.push_back({x, static_cast<float>(row), x + w, 0});
        x += w + gap;
    }
    int rows = row + 1;
    float h = std::min(34.0f, (H() - gap * (rows - 1)) / rows);
    D2D1_RECT_F r = chips[static_cast<size_t>(i)];
    float top = r.top * (h + gap);
    return {r.left, top, r.right, top + h};
}

int ModeGrid::IndexAt(D2D1_POINT_2F p) const
{
    for (int i = 0; i < static_cast<int>(names.size()); i++) if (Contains(Tile(i), p)) return i;
    return -1;
}

void ModeGrid::Paint(Canvas& c)
{
    if (iconsOnly) {
        IDWriteTextFormat* big = Fonts::Glyphs(12);
        for (int i = 0; i < static_cast<int>(names.size()); i++) {
            D2D1_RECT_F r = Tile(i);
            bool on = i == selected;
            float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
            c.FillCircle(cx, cy, Width(r) / 2, on ? Theme::Accent() : i == hoverIndex ? ChipHover : ChipFill);
            c.Glyph(glyphs[i][0], big, r, on ? Palette::Argb(13, 42, 58) : i == hoverIndex ? Theme::Fg : Theme::Dim);
        }
        // The name: of the one under the pointer while it is there (dim, unless it is the chosen one), else the chosen.
        int named = hoverIndex >= 0 ? hoverIndex : selected;
        if (named < 0) return;
        D2D1_RECT_F r = Tile(named);
        float cx = (r.left + r.right) / 2, w = TextWidth(names[named], Fonts::Small()) + 4;
        float x = std::clamp(cx - w / 2, 0.0f, std::max(0.0f, W() - w));
        c.Text(names[named], Fonts::Small(), {x, r.bottom + 2, x + w, H()}, named == selected ? Theme::Accent() : Theme::Dim);
        return;
    }
    IDWriteTextFormat* glyphFont = Fonts::Glyphs(11);
    int chosenFill = Blend(Theme::Card, Theme::Accent(), 0.24f);
    for (int i = 0; i < static_cast<int>(names.size()); i++) {
        D2D1_RECT_F r = Inset(Tile(i), 0.5f, 0.5f);
        bool on = i == selected;
        float radius = Height(r) / 2;
        c.FillRound(r, radius, on ? chosenFill : i == hoverIndex ? ChipHover : ChipFill);
        if (on) c.OutlineRound(r, radius, Theme::Accent(), 1);
        // The icon and the name side by side, centred together (the name cut short if it can't fit).
        float glyphW = TextWidth(glyphs[i], glyphFont), space = 7, pad = 12;
        float nameW = std::min(Width(r) - 2 * pad - glyphW - space, TextWidth(names[i], Fonts::Small()) + 1);
        float x = r.left + (Width(r) - glyphW - space - nameW) / 2;
        c.Glyph(glyphs[i][0], glyphFont, {x, r.top, x + glyphW, r.bottom}, on ? Theme::Accent() : Theme::Dim);
        c.Text(names[i], Fonts::Small(), {x + glyphW + space, r.top, x + glyphW + space + nameW, r.bottom}, on ? Theme::Fg : Theme::Dim);
    }

}

void ModeGrid::MouseMove(D2D1_POINT_2F p, bool)
{
    int i = IndexAt(p);
    if (i == hoverIndex) return;
    hoverIndex = i;
    Invalidate();
}

void ModeGrid::MouseLeave()
{
    hoverIndex = -1;
    Invalidate();
}

void ModeGrid::MouseDown(D2D1_POINT_2F p)
{
    int i = IndexAt(p);
    if (i < 0) return;
    // (Picked again even when it is the one already chosen: choosing a mode puts its values back.)
    selected = i;
    Invalidate();
    if (onChange) onChange(i);
}

// ---- TilePicker ----

TilePicker::TilePicker(int count, int sel) : count(count), awake(count)
{
    selected = sel;
    cursor = Cursor::Hand;
    for (int i = 0; i < count; i++) awake[i] = Target(i);
}

int TilePicker::IndexAt(float x) const
{
    int i = static_cast<int>(x / (Side() + Gap()));
    return i >= 0 && i < count && x - i * (Side() + Gap()) <= Side() ? i : -1;
}

void TilePicker::Animate()
{
    last = GetTickCount64();
    StartTimer(15);
    Invalidate();
}

void TilePicker::Timer()
{
    ULONGLONG now = GetTickCount64();
    float dt = std::min(0.05f, (now - last) / 1000.0f), step = 4 * dt;
    last = now;
    bool moving = false;
    for (int i = 0; i < count; i++) {
        float t = Target(i);
        awake[i] = t > awake[i] ? std::min(t, awake[i] + step) : std::max(t, awake[i] - step);
        if (awake[i] != t) moving = true;
    }
    Invalidate();
    if (!moving) StopTimer();
}

int TilePicker::CaptionLines(const std::vector<std::wstring>& englishNames, float room)
{
    for (const auto& name : englishNames)
        if (TextWidth(Lang::T(name), Fonts::Small()) > room - 2) return 2;
    return 1;
}

void TilePicker::SetCaptions(std::vector<std::wstring> englishNames, int lines)
{
    captions = Translated(std::move(englishNames));
    captionLines = std::max(1, lines);
    Invalidate();
}

void TilePicker::Paint(Canvas& c)
{
    float s = Side();
    for (int i = 0; i < count; i++) {
        D2D1_RECT_F r = Rect(i * (s + Gap()) + 1, 1, s - 2, s - 2);
        bool on = i == selected;
        c.FillRound(r, s * 0.22f, on ? Palette::Argb(52, 52, 56) : i == hoverIndex ? TileHover : TileFill);
        c.OutlineRound(r, s * 0.22f, on ? Theme::Accent() : Palette::Argb(68, 68, 72), on ? std::max(1.5f, s * 0.035f) : 1);
        float k = awake[i] * awake[i] * (3 - 2 * awake[i]);
        if (painter) painter(c, Inset(r, s * 0.12f, s * 0.12f), i, k);
        if (i < static_cast<int>(captions.size())) {   // its name, centred under it (it may reach into the gaps)
            float cx = r.left + s / 2, half = s / 2 + Gap() / 2;
            D2D1_RECT_F box{std::max(0.0f, cx - half), s + 1, std::min(W(), cx + half), H()};   // (kept within the picker)
            c.Text(captions[i], Fonts::Small(), box, on ? Theme::Fg : Theme::Dim, HAlign::Center, captionLines > 1, captionLines > 1);
        }
    }
    if (!Enabled()) c.Fill({0, 0, W(), H()}, Theme::Bg, 170 / 255.0f);   // greyed (e.g. effects the engine can't show)
}

void TilePicker::SetHoverIndex(int i)
{
    if (i == hoverIndex) return;
    hoverIndex = i;
    Animate();
    if (onHover) onHover(i);
}

void TilePicker::MouseMove(D2D1_POINT_2F p, bool) { SetHoverIndex(IndexAt(p.x)); }
void TilePicker::MouseLeave() { SetHoverIndex(-1); }

void TilePicker::MouseDown(D2D1_POINT_2F p)
{
    int i = IndexAt(p.x);
    if (i < 0 || i == selected) return;
    Pick(i);
    Animate();
}

} // namespace bb::ui
