#pragma once
#include "ui/Control.h"
#include <functional>
#include <string>
#include <vector>

namespace bb::ui {

// Controls for picking one of several things. Each keeps `selected` and calls onChange(index) when you pick (setting
// it from code with SetSelected doesn't). Item texts are given in English and shown translated.

// The base: an index, a list of item texts, and the notification.
// The keyboard: the arrow keys step through the items, Home and End go to the first and last.
class Choice : public Control {
public:
    Choice() { focusable = true; }
    int Selected() const { return selected; }
    void SetSelected(int i);
    std::function<void(int)> onChange;
    bool Key(UINT key) override;
    AccessInfo Access() const override;   // a screen reader hears the chosen item's name

protected:
    void Pick(int i);   // chosen with the mouse or keyboard
    virtual std::wstring ItemName(int) const { return L""; }
    virtual int Count() const { return 0; }        // how many items (0: the keys do nothing here)
    virtual void KeyPick(int i) { Pick(i); }       // an item reached with the keyboard
    int selected = 0;
};

// A row of mutually exclusive choices in one rounded box.
class Segmented : public Choice {
public:
    explicit Segmented(std::vector<std::wstring> englishItems);
    void Paint(Canvas& canvas) override;
    void MouseDown(D2D1_POINT_2F p) override;

protected:
    int Count() const override { return static_cast<int>(items.size()); }
    std::wstring ItemName(int i) const override { return items[i]; }

private:
    std::vector<std::wstring> items;
};

// The tabs along the top of the settings: words, or a single icon character (the cog), underlined when chosen.
class TabStrip : public Choice {
public:
    explicit TabStrip(std::vector<std::wstring> englishItems);
    void Paint(Canvas& canvas) override;
    void MouseDown(D2D1_POINT_2F p) override;

protected:
    int Count() const override { return static_cast<int>(items.size()); }
    std::wstring ItemName(int i) const override;

private:
    std::vector<D2D1_RECT_F> ItemBounds() const;
    static bool IsGlyph(const std::wstring& item) { return item.size() == 1 && item[0] >= 0xE000; }
    std::vector<std::wstring> items;
};

// One option in a list of mutually exclusive choices: a radio dot, a title with an optional tag beside it, and a
// one-line note underneath. The owner unchecks the others when onChosen fires.
class ChoiceRow : public Control {
public:
    ChoiceRow(std::wstring_view title, std::wstring_view note, std::wstring_view tag = L"");
    bool Checked() const { return checked; }
    void SetChecked(bool on);
    std::function<void()> onChosen;

    void Paint(Canvas& canvas) override;
    void MouseDown(D2D1_POINT_2F) override;
    bool Key(UINT key) override;   // Space or Enter chooses it
    AccessInfo Access() const override;
    void AccessDo() override { if (onChosen) onChosen(); }

private:
    std::wstring title, note, tag;
    bool checked = false;
};

// A tiny monitor with five spots: the four corners (0 top left, 1 top right, 2 bottom left, 3 bottom right) and the
// centre (4).
class CornerPicker : public Choice {
public:
    explicit CornerPicker(int selected);
    void Paint(Canvas& canvas) override;
    void MouseDown(D2D1_POINT_2F p) override;

protected:
    int Count() const override { return 5; }
    std::wstring ItemName(int i) const override;

private:
    D2D1_RECT_F Frame() const;
    D2D1_POINT_2F Spot(int i) const;
};

// The screen modes as chips (pills that flow onto as many rows as they need), each an icon beside its name; the
// chosen one is tinted and outlined in the accent. Selected can be -1 (none).
class ModeGrid : public Choice {
public:
    ModeGrid(std::vector<std::wstring> glyphs, std::vector<std::wstring> englishNames);
    // One bar split into segments instead of chips, each an icon over its name with roomy padding (a segment as wide
    // as its name needs, the room left shared out). tints: a soft colour for each icon (the chosen one's segment is
    // washed in it), or none for the accent.
    bool bar = false;
    std::vector<int> tints;
    static float BarHeight();   // the bar's height
    void Paint(Canvas& canvas) override;
    void MouseMove(D2D1_POINT_2F p, bool) override;
    void MouseLeave() override;
    void MouseDown(D2D1_POINT_2F p) override;
    bool Key(UINT key) override;   // and Space or Enter puts the chosen mode's values back

protected:
    int Count() const override { return static_cast<int>(names.size()); }
    std::wstring ItemName(int i) const override { return names[i]; }
    void KeyPick(int i) override;

private:
    D2D1_RECT_F Tile(int i) const;
    int IndexAt(D2D1_POINT_2F p) const;
    std::vector<std::wstring> glyphs, names;
    int hoverIndex = -1;
};

// A row of square tiles, each drawn by `painter` (canvas, inner area, index, how awake it is 0..1): the mini icons, or
// the colour effects' previews. The chosen tile has an accent edge; the one under the pointer wakes up (animated).
// onHover gives the index under the pointer (-1 when none), e.g. for a tip.
class TilePicker : public Choice {
public:
    TilePicker(int count, int selected);
    std::function<void(Canvas&, const D2D1_RECT_F&, int, float)> painter;
    std::function<void(int)> onHover;
    int Hovered() const { return hoverIndex; }
    // A name under each tile, on one line or two (the tiles shrink to make room: CaptionHeight(lines) less).
    void SetCaptions(std::vector<std::wstring> englishNames, int lines = 1);
    // How many lines the names need under tiles `room` wide (96-dpi units: a tile and a gap): 2 when one of them,
    // translated, doesn't fit on one.
    static int CaptionLines(const std::vector<std::wstring>& englishNames, float room);
    static float CaptionHeight(int lines) { return lines * 16.0f + 2; }

    void Paint(Canvas& canvas) override;
    void MouseMove(D2D1_POINT_2F p, bool) override;
    void MouseLeave() override;
    void MouseDown(D2D1_POINT_2F p) override;
    void Timer() override;
    void EnabledChanged() override { Invalidate(); }

protected:
    int Count() const override { return count; }
    std::wstring ItemName(int i) const override;   // its caption, or its number
    void KeyPick(int i) override;

private:
    float Side() const { return captions.empty() ? H() : H() - CaptionHeight(captionLines); }   // a tile's side
    float Gap() const { return Side() * 0.28f; }
    float Target(int i) const { return i == hoverIndex ? 1.0f : i == selected ? 0.75f : 0.45f; }
    int IndexAt(float x) const;
    void Animate();
    void SetHoverIndex(int i);
    int count, hoverIndex = -1;
    std::vector<float> awake;
    std::vector<std::wstring> captions;
    int captionLines = 1;
    ULONGLONG last = 0;
};

} // namespace bb::ui
