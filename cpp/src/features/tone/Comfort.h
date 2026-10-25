#pragma once
#include "core/Settings.h"
#include "platform/Window.h"
#include <functional>

namespace bb {

// The Standard engine's presets: a no-sliders curve built only from stages the display hardware applies anyway, so it
// adds no delay and no per-frame work.
//  - Gamma curve x^g: pushes darks down while spreading bright shades apart, so the screen gets darker but text and
//    edges get crisper. Every channel is raised to the same power, which keeps hues.
//  - Colour-filter scale: lowers whites and bright colours evenly, which keeps every colour's proportions exactly.
//  - Every two seconds (when "dim more when the screen is mostly white" is on) it looks at a 32x20 thumbnail of the
//    screen; mostly bright content (white pages) is dimmed up to 15% more, eased in over about a second.
class Comfort : public Window {
public:
    Comfort();
    ~Comfort() override;

    void Apply(const Settings& s, bool active);
    double White() const;                 // where white ends up right now
    bool Adapting() const { return adapt < 0.995; }
    double Scale() const;                 // the colour filter runs before the curve, so it scales by White^(1/g)
    double Net(double x) const;           // input brightness -> what is shown, for the graph

    std::function<void()> changed;        // the scale moved: apply the screen again
    std::function<float()> currentGain;   // how much the colour filter dims right now, undone when measuring

private:
    LRESULT OnMessage(UINT message, WPARAM w, LPARAM l) override;
    void Sample();
    void Ease();
    double ScreenMean() const;
    int level = 1;
    double adapt = 1, target = 1;
    bool sampling = false, easing = false;
};

} // namespace bb
