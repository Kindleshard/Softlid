#pragma once
#include "platform/Window.h"
#include <memory>
#include <vector>

namespace bb {

// A see-through black sheet over every monitor: clicks pass through it. Used to dim the screen behind breaks and,
// with a blink, to fade the screen like a real blink (an on-screen shade: the backlight is never touched).
class ScreenShade {
public:
    ScreenShade();
    ~ScreenShade();   // (out of line: Sheet is only known in the .cpp)
    void Show(float darkness);      // 0..1
    void SetDarkness(float darkness);
    void Hide();
    bool Showing() const { return showing; }

private:
    class Sheet;
    std::vector<std::unique_ptr<Sheet>> sheets;
    bool showing = false;
};

} // namespace bb
