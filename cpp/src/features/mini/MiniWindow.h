#pragma once
#include "platform/Window.h"
#include "ui/Graphics.h"
#include "ui/LayeredCanvas.h"
#include <memory>
#include <string>

namespace bb {

class MiniHost;

// Mini mode: the settings window folded into a round icon that stays on top. Hovering opens a small panel beside it
// with brightness, the blue light filter, the backlight (when that is on), the blink reminder switch and the focus
// timer; clicking the icon opens the full settings again; dragging moves it. The icon is a per-pixel-alpha window, so
// its circle is smooth on any background. Left alone, it fades to the opacity set in Options (after the delay and
// over the time set there). Its rim fills as the next break comes closer; while the focus timer runs, the time left
// takes the icon's place and the disc steps in for a second circle round it: one focus round, the work period then the
// rest paler, in the colour set in Options (green unless set), with a highlight running along the work part.
class MiniWindow : public Window {
public:
    // hidden: made, but not shown until Reveal (while the settings window folds into it: see MiniMorph)
    explicit MiniWindow(MiniHost& app, bool hidden = false);
    ~MiniWindow() override;
    void Reveal();
    RECT Bounds() const;        // where it is on screen
    void ShowResizer();         // its menu's Resize: a slider for its size, under it

private:
    LRESULT OnMessage(UINT message, WPARAM w, LPARAM l) override;

    void Draw();
    void DrawClock(ID2D1RenderTarget* t, const std::wstring& time, bool resting);
    void DrawLap(ID2D1RenderTarget* t, float cx, float cy, float r, float ring, float work, float rest);
    void DrawFlow(ID2D1RenderTarget* t, float cx, float cy, float r, float ring, float progress);
    void DrawBreath(ID2D1RenderTarget* t, float cx, float cy, float r, float ring);
    void DrawMinimize(ID2D1RenderTarget* t, float k);       // the small Minimize button, as hovered (k: 0..1)
    D2D1_ELLIPSE MinimizeSpot() const;                       // where it is (96-dpi units)
    bool OnMinimize(LPARAM point) const;                     // a mouse message's point is on it (while it shows)
    float Progress() const;     // the rim's arc: the wait for the next look-away break (when shown), or -1
    float FocusDone() const;    // how far into the focus timer's work period while it runs, or -1
    int FocusColor() const;     // the focus timer circle's colour (as set in Options; green unless set)
    bool FocusOn() const;
    bool Flowing() const;
    float RestDone() const;     // the focus timer's rest, while it shows: how far into it (0..1), or -1
    std::wstring Clock() const; // what takes the icon's place: the focus timer's time left (work or rest), or nothing
    float Bright() const;

    void CheckHover();          // every 120 ms
    void FadeStep();            // every 15 ms while waiting to fade, or fading
    void AnimStep();            // while the icon animates, the glow breathes or the focus timer's highlight runs
    void StartAnim();
    void Settle();      // still now: trims memory a few seconds later
    void SetLit(bool on);
    void Wake();
    void Rest();
    void OpenPanel();
    POINT StartingPoint() const;
    void Resize();              // to the size set (Options, or Resize from its menu), round its middle

    MiniHost& app;
    class Panel;
    std::unique_ptr<Panel> panel;
    class Resizer;
    std::unique_ptr<Resizer> resizer;
    static constexpr float BaseSize = 56 * 1.3f;   // 96-dpi units, at 100% (what was 130% before MiniScale)
    LayeredCanvas canvas;
    gfx::ComPtr<IDWriteTextFormat> clockFont, clockFontBig;
    float scale = 1, size = 56, gap = 10;   // size and gap in 96-dpi units
    int px = 84;                            // the window's side in pixels
    POINT pos{};
    bool lit = false, pressed = false, dragged = false, animating = false, fading = false;
    bool minimizeHot = false, minimizePressed = false;   // the pointer on the Minimize button; pressed on it
    POINT grab{};
    ULONGLONG idleSince = 0, lastAnim = 0, nextBreath = 0, breathStart = 0;
    ULONGLONG nextFlow = 0, flowStart = 0;   // the focus timer's highlight: one run now and then (see Flowing)
    double shown = 255, awake = 0;
    float drawnBright = -1, drawnWarm = -1, drawnProgress = -2, drawnRest = -2, drawnFocus = -2;
    int drawnIcon = -1, tick = 0;
    std::wstring drawnClock;
};

} // namespace bb
