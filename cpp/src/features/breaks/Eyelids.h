#pragma once
#include "core/Settings.h"
#include "platform/Window.h"
#include <memory>
#include <vector>

namespace bb {

// The immersive blink: on every screen, a dark lid slides down from the top and another up from the bottom, meet (or
// nearly, by the depth setting) and part again, like your own eyelids. Each lid is drawn once per screen size, with
// a gently curved, soft rim, and the animation only moves the two windows; they go away after each blink.
class Eyelids : public Window {
public:
    Eyelids();
    ~Eyelids() override;
    void Play(const Settings& s);

private:
    LRESULT OnMessage(UINT message, WPARAM w, LPARAM l) override;   // (a hidden window, for the frame timer)
    void Step();
    void Place(double closed);   // 0: out of sight, 1: their edges meet in the middle
    class Lid;
    std::vector<std::unique_ptr<Lid>> lids;
    std::vector<RECT> screens;
    ULONGLONG started = 0;
    double depth = 1;
};

} // namespace bb
