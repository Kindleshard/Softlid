#pragma once
#include "features/tone/SmartGpu.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace bb {

// Smart (GPU): local tone mapping on the graphics card. It lowers the screen's overall contrast (big bright areas, big
// dark areas) while keeping local contrast (text, edges, image detail, neighbouring colours) intact. A worker thread
// tone-maps each new desktop image (see SmartGpu) into a click-through, always-on-top window that is excluded from
// capture, so it never sees itself. Only processed frames are ever shown, so bright content can't flash; the price is
// about one refresh of delay. If it can't run, it posts `failMessage` to `notify` (the app then switches to Standard).
class SmartFilter {
public:
    SmartFilter();
    ~SmartFilter();
    SmartFilter(const SmartFilter&) = delete;
    SmartFilter& operator=(const SmartFilter&) = delete;

    void Apply(const SmartGpu::Params& p, HWND notify, UINT failMessage);   // starts it if needed, otherwise updates it
    void Stop();
    void ClearFailure() { failed = false; }
    void KeepOnTop();   // other always-on-top windows (the taskbar) can rise above it; called once a second
    bool Failed() const { return failed; }
    bool Running() const { return running; }
    std::wstring Status() const;

private:
    void Run(HWND overlay, RECT bounds);
    void Fail(const std::wstring& why);
    void SetStatus(const std::wstring& text);

    class Overlay;
    std::unique_ptr<Overlay> overlay;
    std::thread thread;
    std::atomic<bool> running{false}, failed{false};
    mutable std::mutex gate;
    SmartGpu::Params pending;
    bool dirty = false;
    LONGLONG changedAt = 0;
    std::wstring status;
    HWND notify = nullptr;
    UINT failMessage = 0;
};

} // namespace bb
