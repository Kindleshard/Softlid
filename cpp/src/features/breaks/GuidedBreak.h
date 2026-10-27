#pragma once
#include "core/SessionPlan.h"
#include "core/Settings.h"
#include "features/breaks/Liquid.h"
#include "platform/Voice.h"
#include "platform/Window.h"
#include "ui/Graphics.h"
#include "ui/LayeredCanvas.h"
#include <memory>
#include <optional>
#include <vector>

namespace bb {

// A look-away break with eye exercises, on the whole screen: it fades to black (nothing left to look at), then a drop
// of water with an eye in it fades in, looking at you, and acts out each exercise like a cartoon liquid:
// - left and right: it glances the way it will go, then shoots across, slaps against the side and sticks; to leave it
//   has to pull free, stretching on a sticky neck until it snaps loose and whips across to the other side
// - up and down: it falls to the floor and splats, then gets sucked up to the ceiling on a long thread, hangs there a
//   moment and drips down again
// - the circle: it slides round the screen leaving a glossy trail behind it like a snail, which beads up and dries
// - closing: it turns into a real eye, with lids that close and open
// Each movement takes at most 3 s; with more time it repeats. Spoken lines (when on) are never cut: the steps are
// timed around them. Esc or Skip ends it, Snooze waits 5 min (neither when breaks are forced). When it is over it posts
// BreakCard::DoneMessage to its owner (a look-away break, with the snooze length); the owner then deletes it.
class GuidedBreak : public Window {
public:
    GuidedBreak(HWND owner, const Settings& s, float warmth);
    ~GuidedBreak() override;

private:
    LRESULT OnMessage(UINT message, WPARAM w, LPARAM l) override;
    void Frame();
    void SetFade(double f);
    void Redraw();
    void UpdateCaption();
    void ShowCaption();
    double Now() const;
    void End(int snoozeSeconds);   // Skip (0), or Snooze: it fades out, then tells the owner

    // Motion (each exercise's choreography).
    struct Timing { double pre, cycle, moves, back; int reps; };
    static Timing Plan(double len, double pre, double back);
    double Radius() const { return std::min(width, height) * 0.075; }
    double CX() const { return width / 2.0; }
    double CY() const { return height / 2.0; }
    void Animate(int k, double u, double len, double dt, double t);
    void Trail(double t, double R);
    void Appear(double u);
    void Glance(double u, double lx, double ly);
    void SetFilm(Liquid::Film& f, int edge, double at, double amount, double spread);
    void Sides(double u, double len);
    void WallLeg(bool fromMiddle, int dir, double v, double dur);
    void BackFromSide(int side, double v, double dur);
    void UpDown(double u, double len);
    void FallAndRise(bool first, double w, double c);
    void DripHome(double v);
    void Circle(double u, double len);
    void Blink(double u, double len);

    class Layer;
    class Buttons;
    HWND owner;
    bool sounds, forced, backdrop, finished = false;
    std::unique_ptr<Voice> voice;
    std::optional<SessionPlan> plan;
    Liquid liquid;
    std::vector<double> trailBorn;
    std::vector<std::unique_ptr<Layer>> shades;   // the other screens, dark
    std::unique_ptr<Layer> drop, caption;          // see-through windows above the break: the liquid, and the words
    std::unique_ptr<Buttons> buttons;               // Skip and Snooze (not when breaks are forced)
    LayeredCanvas captionArt;
    gfx::ComPtr<IDWriteTextFormat> captionFont, hintFont;
    RECT bounds{};
    int width = 0, height = 0;
    float scale = 1;
    bool liquidShown = false, started = false;
    double fadeK = 0, outroAt = -1, fadeAt = -1, lastT = 0, trailX = 0, trailY = 0;
    int step = -2, spoken = -1, shownSeconds = -1, trailNext = 0;
    int snooze = 0;
    float alpha = 0;
    LARGE_INTEGER clockStart{}, frequency{};

    // What the current moment asks of the drop.
    double px = 0, py = 0, mainM = 1, grow = 1, flatX = 1, flatY = 1, tail = 1, stiff = 1, eyeShape = 0, open = 1, lookTX = 0, lookTY = 0;
    int axis = -1;                              // stretch along x (0), y (1), the way it moves (2), or not at all (-1)
    double anchorX = 0, anchorY = 0, stick = 0; // a point on a wall the tail is still stuck to, and how firmly
    bool trailOn = false;
    // Its own momentum: a stretch that lags behind the speed, and eyes that swing to where they look.
    double prevX = 0, prevY = 0, stretch = 1, stretchSpeed = 0, lookX = 0, lookY = 0, lookVX = 0, lookVY = 0;
};

} // namespace bb
