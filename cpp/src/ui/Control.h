#pragma once
#include "ui/Canvas.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace bb::ui {

class Container;
class DirectWindow;

enum class Cursor { Arrow, Hand, Cross };

// What a screen reader is told about a control (ui/Access.h passes it on to Windows).
enum class Role { None, Text, Button, Link, Switch, Slider, Spinner, Choice, Radio };
struct AccessInfo {
    Role role = Role::None;   // None: screen readers skip it
    std::wstring name;        // empty: the first label in its row names it
    std::wstring value;       // what it is set to, in words (a choice's item, a colour)
    bool on = false;          // a switch that is on, a radio row that is chosen
    int expanded = -1;        // a button that folds something: 1 open, 0 folded (-1: not that kind)
    double now = 0, min = 0, max = 0, step = 1;   // a slider or stepper
};

// Something drawn inside a DirectWindow, with no window of its own: it paints itself with Direct2D and gets the mouse
// and keyboard from the window. Geometry is in 96-dpi units, relative to the parent; everything a control is told
// (mouse positions, painting) is in its own coordinates, (0, 0) being its top left.
class Control {
public:
    Control() = default;
    virtual ~Control();
    Control(const Control&) = delete;
    Control& operator=(const Control&) = delete;

    void SetBounds(float x, float y, float w, float h);
    const D2D1_RECT_F& Bounds() const { return bounds; }
    float W() const { return bounds.right - bounds.left; }
    float H() const { return bounds.bottom - bounds.top; }

    bool Visible() const { return visible; }
    void SetVisible(bool on);
    bool Enabled() const;   // false when it or a parent is switched off
    void SetEnabled(bool on);

    std::wstring tip;                // shown when the pointer rests on it (already translated)
    Cursor cursor = Cursor::Arrow;
    bool focusable = false;          // takes the keyboard (clicked, or reached with Tab)

    Container* Parent() const { return parent; }
    DirectWindow* Window() const;    // null until added to a window
    void Invalidate();               // drawn again soon
    bool Focused() const;
    void Focus();
    bool Hovered() const { return hover; }

    // Where it is in the window (96-dpi units), and the part of it that can be seen there (not scrolled away).
    D2D1_POINT_2F Origin() const;
    D2D1_RECT_F VisibleRect() const;

    void StartTimer(UINT milliseconds);   // Timer() is called until StopTimer
    void StopTimer();

    // What a control does; all optional.
    virtual void Paint(Canvas&) {}
    virtual void MouseDown(D2D1_POINT_2F) {}          // the left button (the control has the mouse until it is released)
    virtual void MouseMove(D2D1_POINT_2F, bool) {}    // bool: the left button is held down on this control
    virtual void MouseUp(D2D1_POINT_2F) {}
    virtual void Click() {}                           // pressed and released over it
    virtual void MouseEnter() { Invalidate(); }
    virtual void MouseLeave() { Invalidate(); }
    virtual bool Wheel(int) { return false; }         // true: used (otherwise it goes to the parent, e.g. the page)
    virtual bool Key(UINT) { return false; }          // a key while focused; true: used
    virtual void FocusChanged() { Invalidate(); }
    virtual void EnabledChanged() { Invalidate(); }
    virtual void Timer() {}
    virtual void Placed() {}                          // its place on screen may have changed (native children follow)
    virtual void Notify(WORD) {}                      // from its native child window (a text box: EN_CHANGE, ...)
    virtual AccessInfo Access() const { return {}; }  // what a screen reader is told about it
    virtual void AccessDo() { Click(); }              // pressed, turned or chosen by a screen reader
    virtual void AccessSet(double) {}                 // a slider or stepper set by a screen reader

    // The control under a point (in the parent's coordinates), or null.
    virtual Control* HitTest(D2D1_POINT_2F p);
    virtual void PaintTree(Canvas& canvas, const D2D1_RECT_F& clip);

protected:
    bool hover = false;   // the pointer is over it

private:
    friend class Container;
    friend class DirectWindow;
    D2D1_RECT_F bounds{};
    Container* parent = nullptr;
    bool visible = true, enabled = true;
};

// A control holding others (a page, a row). Children are kept in the order added, which is also the Tab order.
class Container : public Control {
public:
    template <class T, class... Args>
    T& Add(Args&&... args)
    {
        auto child = std::make_unique<T>(std::forward<Args>(args)...);
        T& added = *child;
        Adopt(std::move(child));
        return added;
    }
    void Clear();
    const std::vector<std::unique_ptr<Control>>& Children() const { return children; }

    // How far the children are moved up and left (a scrolling page), in 96-dpi units.
    virtual D2D1_POINT_2F ScrollOffset() const { return {0, 0}; }

    Control* HitTest(D2D1_POINT_2F p) override;
    void PaintTree(Canvas& canvas, const D2D1_RECT_F& clip) override;
    void Placed() override;
    void EnabledChanged() override;

protected:
    virtual void PaintOver(Canvas&) {}   // drawn after (on top of) the children

private:
    friend class Control;
    friend class DirectWindow;
    void Adopt(std::unique_ptr<Control> child);
    std::vector<std::unique_ptr<Control>> children;
    DirectWindow* window = nullptr;   // set on a window's root only
};

} // namespace bb::ui
