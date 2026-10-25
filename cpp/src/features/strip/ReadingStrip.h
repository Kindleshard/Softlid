#pragma once
#include "core/Settings.h"
#include <windows.h>
#include <memory>

namespace bb {

// Dims the screen except a clear area: a horizontal or vertical band, a rounded rectangle or a circle. The clear
// area follows the mouse, or stays put and moves with the keys.
// One tinted sheet covers the whole monitor and never moves or resizes; the clear area is a hole cut out of it, so
// moving it is a single change and the dim can't tear or leave gaps. A soft edge or an outline is drawn once into a
// small per-pixel window that fills the hole and only moves. Bands can dim just one side.
// Following the mouse uses raw input (Windows tells `listener` when the mouse moves, as WM_INPUT): no hook, and
// nothing at all while the mouse is still.
class ReadingStrip {
public:
    ReadingStrip();
    ~ReadingStrip();
    ReadingStrip(const ReadingStrip&) = delete;
    ReadingStrip& operator=(const ReadingStrip&) = delete;

    void Apply(const Settings& s, HWND listener);   // shows, updates or hides it as set
    void MouseMoved();                              // the listener got WM_INPUT
    void Nudge(int dx, int dy);                     // the movement keys: a few pixels a press, faster while held

private:
    void Place();
    void Stop();
    void Listen(HWND listener, bool on);

    class Sheet;
    class Tile;
    std::unique_ptr<Sheet> dim;
    std::unique_ptr<Tile> tile;
    const Settings* s = nullptr;
    float scale = 1;
    RECT screen{};
    POINT at{};
    bool placed = false, listening = false;
    ULONGLONG lastNudge = 0;
    double speed = 0;
};

} // namespace bb
