#pragma once
#include "core/Settings.h"
#include "platform/Window.h"
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace bb {

// Film grain: fine monochrome noise in a click-through window over each screen. It is drawn once. When animated,
// the window is nudged to a random offset 24 times a second; the noise is random, so a shifted copy looks like
// fresh grain, and a window move costs far less than redrawing millions of pixels.
class Grain : public Window {
public:
    Grain();
    ~Grain() override;

    // baked: the smart layer is drawing the grain itself, so these windows stand down.
    void Apply(const Settings& s, float dpiScale, bool baked);
    void Invalidate() { key = L"?"; }   // after a display change: redraw for the new screens
    void Close();

private:
    class Sheet;
    LRESULT OnMessage(UINT message, WPARAM w, LPARAM l) override;
    void Build();
    void Jitter();

    std::vector<std::unique_ptr<Sheet>> sheets;
    std::mt19937 random{std::random_device{}()};
    int strength = 0, tone = 1;
    bool coarse = false;
    float scale = 1;
    std::wstring key = L"off";
};

} // namespace bb
