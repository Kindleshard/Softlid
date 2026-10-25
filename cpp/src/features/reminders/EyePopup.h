#pragma once
#include "core/Settings.h"
#include "platform/Window.h"
#include "ui/LayeredCanvas.h"
#include "ui/ScreenShade.h"

namespace bb {

// The blink reminder: an eye in a corner that fades in, blinks once and fades out (about two seconds). It never takes
// focus and clicks pass through it. Optionally the whole screen dims in step with the blink.
class EyePopup : public Window {
public:
    // instant skips the fade-in, so live changes to its size, opacity and place show straight away.
    void Play(const Settings& s, bool instant);

private:
    LRESULT OnMessage(UINT message, WPARAM w, LPARAM l) override;
    void Step();
    void Render();

    LayeredCanvas canvas;
    ScreenShade shade;
    POINT at{};
    int width = 0, height = 0, elapsed = 0;
    float scale = 1, opacity = 1, fadeDepth = 1;
    bool fadeScreen = false;
};

} // namespace bb
