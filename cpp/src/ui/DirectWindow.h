#pragma once
#include "platform/Window.h"
#include "ui/Control.h"
#include "ui/SoftCanvas.h"
#include <functional>
#include <set>

namespace bb::ui {

// A window drawn with Direct2D (in software: no graphics-card device, so it stays light), holding controls. It
// follows each monitor's scaling (per-monitor DPI): controls are laid out in 96-dpi units and drawn sharp at any
// scale. It routes the mouse, the keyboard (Tab moves between controls), the wheel, timers and tooltips to them.
class DirectWindow : public bb::Window {
public:
    struct Options {
        const wchar_t* title = L"BlinkBuddy";
        bool popup = false;              // no title bar, a thin edge and a shadow (e.g. the colour picker)
        bool resizable = false;          // taller or shorter (the width stays)
        bool closeOnDeactivate = false;  // closes when you click anywhere else
        bool dialog = false;             // a title bar with only a close button
        bool edge = true;                // a pop-up's thin edge
        int background = 0;              // ARGB; 0: the usual dark
        float minHeight = 0;             // 96-dpi units, for a resizable window
        float zoom = 1;                  // its own size on top of the display's scaling (e.g. 0.8 to 1.2)
        HWND owner = nullptr;
        HICON icon = nullptr;
    };

    DirectWindow();
    ~DirectWindow() override;

    // Creates the window with a client area of w x h (96-dpi units). at: the top left on screen (pixels), or null to
    // centre it on the monitor under the pointer. It shows when Show is called.
    void Open(float w, float h, const POINT* at, const Options& options);
    void Show(bool activate = true);
    void Close();                         // as if closed by its X: Closed() runs, then the window is destroyed
    std::function<void()> onClosed;       // after the window is gone (to free this object from here: DeleteLater)

    Container& Root() { return root; }
    float Scale() const { return dpi / 96.0f * options.zoom; }   // pixels per 96-dpi unit (the zoom included)
    D2D1_SIZE_F ClientSize() const;       // 96-dpi units
    POINT ToScreen(D2D1_POINT_2F windowPoint) const;   // pixels
    RECT ToPixels(const D2D1_RECT_F& r) const;         // window client pixels

    void Invalidate(const D2D1_RECT_F& windowRect);
    void InvalidateAll();
    Control* Focused() const { return focused; }
    void SetFocus(Control* control);
    void Placed();                        // after controls move: native child windows follow
    void Relayout();                      // Layout() then Placed() and a fresh paint

    // Used by controls.
    void StartTimer(Control* control, UINT milliseconds);
    void StopTimer(Control* control);
    void Forget(Control* control, bool timersToo = true);   // a control is going away or hiding
    HBRUSH EditBrush();                   // the dark background of native text boxes

protected:
    virtual void Layout() {}              // the client area changed size or scale: place the controls
    virtual bool Shortcut(UINT) { return false; }   // a key no control used (Esc, Enter); true: used
    LRESULT OnMessage(UINT message, WPARAM w, LPARAM l) override;

private:
    D2D1_POINT_2F ToDips(LPARAM l) const;
    void Paint();
    void MoveFocus(bool back);
    void SetHover(Control* control);
    void UpdateTip();
    void ReleaseGraphics();

    // The messages OnMessage hands on.
    void OnSize(WPARAM w, LPARAM l);
    void OnDpiChanged(WPARAM w, LPARAM l);
    void OnMinMaxInfo(MINMAXINFO* info);
    void OnMouseMove(D2D1_POINT_2F p);
    void OnMouseLeave();
    void OnLeftDown(D2D1_POINT_2F p);
    void OnLeftUp(D2D1_POINT_2F p);
    void OnCaptureChanged(HWND newOwner);
    void OnWheel(WPARAM w, LPARAM l);
    void SetPointer();
    void OnKeyDown(UINT key);
    void OnCommand(WPARAM w, LPARAM l);
    LRESULT OnEditColors(HDC dc);
    void OnDestroy();
    LRESULT OnFinalDestroy(WPARAM w, LPARAM l);

    Container root;
    UINT dpi = 96;
    Options options;
    SoftCanvas canvas;   // (its drawing, on the shared software device)
    Control* hover = nullptr;
    Control* captured = nullptr;
    Control* focused = nullptr;
    std::set<Control*> timers;
    HWND tooltip = nullptr;
    HBRUSH editBrush = nullptr;
    bool tracking = false, dying = false;
    float fixedWidth = 0;   // pixels, for a resizable window
};

} // namespace bb::ui
