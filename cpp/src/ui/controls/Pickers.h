#pragma once
#include "ui/controls/Choices.h"
#include <functional>
#include <string>
#include <vector>

namespace bb::ui {

// A choice from a short list, as a pill showing the current one; clicking opens the list (a dark menu).
class DropDown : public Choice {
public:
    explicit DropDown(std::vector<std::wstring> englishItems);
    void SetItems(std::vector<std::wstring> englishItems);
    void SetPlainItems(std::vector<std::wstring> items);   // as they are (names of languages, of cities)

    void Paint(Canvas& canvas) override;
    void Click() override;
    bool Key(UINT key) override;

protected:
    int Count() const override { return static_cast<int>(items.size()); }
    std::wstring ItemName(int i) const override { return items[i]; }

private:
    void Open();
    std::vector<std::wstring> items;
};

// "More options" under a section: a small link with a chevron that folds the less-used settings in and out.
class Disclosure : public Control {
public:
    explicit Disclosure(bool open);
    bool Open() const { return open; }
    std::function<void(bool)> onToggle;

    void Paint(Canvas& canvas) override;
    void Click() override;
    bool Key(UINT key) override;
    AccessInfo Access() const override;

private:
    bool open;
};

// The chevron on a section's heading: up while the section is open (click to fold it), down while it is folded.
class FoldButton : public Control {
public:
    explicit FoldButton(bool open);
    bool Open() const { return open; }
    void SetOpen(bool on);
    std::function<void()> onClick;

    void Paint(Canvas& canvas) override;
    void Click() override;
    bool Key(UINT key) override;   // Space or Enter
    AccessInfo Access() const override;   // (named by its section's title)

private:
    bool open;
};

// A number with - and + on either side of it, under a small label (held down, it keeps stepping).
class Stepper : public Control {
public:
    Stepper(std::wstring_view englishLabel, int min, int max, int step, int value, std::function<std::wstring(int)> format);
    int Value() const { return value; }
    void SetValue(int v);
    std::function<void(int)> onChange;

    void Paint(Canvas& canvas) override;
    void MouseMove(D2D1_POINT_2F p, bool) override;
    void MouseLeave() override;
    void MouseDown(D2D1_POINT_2F p) override;
    void MouseUp(D2D1_POINT_2F) override;
    void Timer() override;
    bool Key(UINT key) override;   // Left or Down one step less, Right or Up one more
    AccessInfo Access() const override;
    void AccessSet(double v) override;

private:
    float LabelHeight() const { return H() * 0.36f; }
    int SideAt(D2D1_POINT_2F p) const;   // -1 the minus side, 1 the plus side, 0 neither
    void Bump(int side);
    std::wstring label;
    int min, max, step, value, hoverSide = 0, held = 0;
    bool repeating = false;
    std::function<std::wstring(int)> format;
};

// A line of text to type in: Windows' own edit box (for typing, selecting, pasting and IMEs), in a rounded frame drawn
// to match. onChange gets the text as you type.
class TextBox : public Control {
public:
    explicit TextBox(int maxLength = 0, bool upperCase = false);
    ~TextBox() override;
    std::wstring Text() const;
    void SetText(const std::wstring& text);   // (no onChange)
    std::function<void(const std::wstring&)> onChange;
    std::function<void(UINT)> onKey;           // Enter, Esc, Up or Down pressed in it
    std::function<void()> onLeave;             // the caret left it (typing is over)
    std::wstring cue;                          // grey hint shown while it is empty (already translated)
    void StartTyping();                        // the caret goes in

    void Paint(Canvas& canvas) override;
    void Placed() override;
    void Notify(WORD code) override;
    void MouseDown(D2D1_POINT_2F) override;
    void FocusChanged() override;   // reached with Tab: the caret goes in

private:
    static LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    HWND edit = nullptr;
    HFONT font = nullptr;
    UINT fontDpi = 0;
    int maxLength;
    bool upperCase, updating = false;
    std::wstring pending;   // text set before the edit box exists
};

// Input brightness (left to right) against what is shown (bottom to top); the dashed diagonal is "unchanged".
class CurveView : public Control {
public:
    std::function<double(double)> curve;
    void Paint(Canvas& canvas) override;
};

// The last minute of two percentages as lines (the first in the accent color, the second green), newest on the
// right. The scale follows the data (at least 0-5%), so small figures still show; its top is written in the corner.
class UsageGraph : public Control {
public:
    static constexpr size_t Points = 60;
    void Add(double first, double second);   // one sample (a second's worth)
    void Paint(Canvas& canvas) override;

private:
    std::vector<double> first, second;
};

// Six dots to take hold of: dragging them moves what they belong to. onDrag gets the pointer's height in the window
// (96-dpi units) as it moves, onDrop the same when it is let go.
class DragGrip : public Control {
public:
    DragGrip() { cursor = Cursor::Hand; }
    std::function<void(float)> onDrag, onDrop;
    bool Dragging() const { return dragging; }

    void Paint(Canvas& canvas) override;
    void MouseDown(D2D1_POINT_2F p) override;
    void MouseMove(D2D1_POINT_2F p, bool down) override;
    void MouseUp(D2D1_POINT_2F p) override;

private:
    float startY = 0;
    bool dragging = false;
};

// A day from midnight to midnight as a landscape: the sky darkens to night as deep as the night is at each moment
// (with stars, the moon, the sun and hills), a mark at the time now, and the hours under it.
class DayBar : public Control {
public:
    std::function<std::vector<float>()> profile;   // how far into the night it is (0 day .. 1 full night), evenly
                                                  // through the day from midnight
    std::function<int()> now;                      // the minute of the day now
    bool animate = true;                           // the line for now waves gently (not in diagnostic screenshots)
    void Paint(Canvas& canvas) override;
    void Timer() override;

private:
    float lineX = -1, lineH = 0;                   // where the line for now was drawn
    bool waving = false;
};

// Where a dragged item would land: a short bar in the accent colour.
class DropMark : public Control {
public:
    void Paint(Canvas& canvas) override;
};

// The focus timer's face: a ring that fills as the period runs (accent while working, green while resting), the time in
// the middle, what is happening under it, and four dots for the periods done in this round of four.
class FocusRing : public Control {
public:
    std::function<float()> progress = [] { return 0.0f; };
    std::function<std::wstring()> timeText, stateText;
    std::function<int()> ink, done;
    void Paint(Canvas& canvas) override;
};

// Softlid's eye, looking about (the blink settings' preview).
class EyeView : public Control {
public:
    void Set(float lookX, float lookY, float open);
    void Paint(Canvas& canvas) override;

private:
    float lookX = 0, lookY = 0, open = 1;
};

} // namespace bb::ui
