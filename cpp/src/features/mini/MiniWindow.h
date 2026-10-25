#pragma once
#include "platform/Window.h"
#include "ui/Graphics.h"
#include "ui/LayeredCanvas.h"
#include <memory>
#include <string>

namespace bb {

class App;

// Mini mode: the settings window folded into a round icon that stays on top. Hovering opens a small panel beside it
// with brightness, the blue light filter, the backlight (when that is on), the blink reminder switch and the focus
// timer; clicking the icon opens the full settings again; dragging moves it. The icon is a per-pixel-alpha window, so
// its circle is smooth on any background. Left alone, it fades to 65% (after the delay and over the time set in
// Options). Its rim fills as the next break comes closer; while the focus timer runs, the time left takes the icon's
// place and a highlight runs along the rim.
class MiniWindow : public Window {
public:
    explicit MiniWindow(App& app);
    ~MiniWindow() override;

private:
    LRESULT OnMessage(UINT message, WPARAM w, LPARAM l) override;

    void Draw();
    void DrawClock(ID2D1RenderTarget* t, const std::wstring& time);
    void DrawFlow(ID2D1RenderTarget* t, float cx, float cy, float r, float ring, float progress);
    void DrawBreath(ID2D1RenderTarget* t, float cx, float cy, float r, float ring);
    float Progress() const;     // the rim's arc: the wait for the next break (when shown), and the focus timer's
    bool FocusOn() const;
    bool Flowing() const;
    float Bright() const;
    RECT Bounds() const;

    void CheckHover();          // every 120 ms
    void FadeStep();            // every 15 ms while waiting to fade, or fading
    void AnimStep();            // while the icon animates, the glow breathes or the focus timer's highlight runs
    void StartAnim();
    void SetLit(bool on);
    void Wake();
    void Rest();
    void OpenPanel();
    POINT StartingPoint() const;

    App& app;
    class Panel;
    std::unique_ptr<Panel> panel;
    LayeredCanvas canvas;
    gfx::ComPtr<IDWriteTextFormat> clockFont, clockFontBig;
    float scale = 1, size = 56, gap = 10;   // size and gap in 96-dpi units
    int px = 84;                            // the window's side in pixels
    POINT pos{};
    bool lit = false, pressed = false, dragged = false, animating = false, fading = false;
    POINT grab{};
    ULONGLONG idleSince = 0, lastAnim = 0, nextBreath = 0, breathStart = 0;
    double shown = 255, awake = 0;
    float drawnBright = -1, drawnWarm = -1, drawnProgress = -2;
    int drawnIcon = -1, tick = 0;
    std::wstring drawnClock;
};

} // namespace bb
