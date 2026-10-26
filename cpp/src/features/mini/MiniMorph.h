#pragma once
#include "platform/Window.h"
#include "ui/LayeredCanvas.h"
#include <cstdint>
#include <functional>
#include <vector>

namespace bb {

// The settings window folding into mini mode's icon, so you see where it went: a picture of the window shrinks,
// rounds into a circle and flies to the icon's place in about a quarter of a second, darkening into the icon's disc as
// it lands; then the icon shows. The picture is a click-through layered window of its own (the settings window itself
// is gone by then).
class MiniMorph : public Window {
public:
    struct Picture {
        std::vector<uint32_t> pixels;   // premultiplied BGRA, opaque
        int width = 0, height = 0;
        RECT at{};                      // where it was on screen (the window's visible frame)
    };
    static Picture Take(HWND window);              // the window as it looks now (before it closes)

    MiniMorph(Picture picture, RECT to, std::function<void()> landed);
    ~MiniMorph() override;

private:
    LRESULT OnMessage(UINT message, WPARAM w, LPARAM l) override;
    void Frame();

    Picture picture;
    RECT to;
    std::function<void()> landed;
    LayeredCanvas canvas;
    gfx::ComPtr<ID2D1Bitmap> bitmap;
    ID2D1RenderTarget* madeFor = nullptr;
    ULONGLONG start = 0;
};

} // namespace bb
