#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <windows.h>

namespace bb {

// Smart layer: the smart filter's decision (dim each region by how bright its surroundings are, and keep anything
// from ending up brighter than a calmed white page) without redrawing the screen. A few times a second, and only if
// something changed, the graphics card measures a 1/16-resolution brightness map of the desktop; the worker turns it
// into a smooth darkening mask, and one click-through layer (the mask, plus static grain baked in) sits over the
// screen. Apps keep drawing straight to the display, so nothing is delayed, and the layer is only redrawn where the
// mask really changed. Black dims red, green and blue equally, so colours keep their hue.
class SmartLayer {
public:
    struct Params {
        int level = 1;
        float gain = 1;         // dimming the colour filter already applies, undone when measuring
        bool grain = false;
        int grainAlpha = 0, grainTone = 1, grainBlock = 1;
    };

    SmartLayer();
    ~SmartLayer();
    SmartLayer(const SmartLayer&) = delete;
    SmartLayer& operator=(const SmartLayer&) = delete;

    void Apply(const Params& p, HWND notify, UINT failMessage);
    void Stop();
    void ClearFailure() { failed = false; }
    void KeepOnTop();
    bool Failed() const { return failed; }
    std::wstring Status() const;

private:
    void Run(HWND layer, RECT bounds);
    void Fail(const std::wstring& why);
    void SetStatus(const std::wstring& text);

    class Layer;
    std::unique_ptr<Layer> layer;
    std::thread thread;
    std::atomic<bool> running{false}, failed{false};
    mutable std::mutex gate;
    Params pending;
    bool dirty = false;
    std::wstring status;
    HWND notify = nullptr;
    UINT failMessage = 0;
};

} // namespace bb
