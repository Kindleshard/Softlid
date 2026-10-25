#include "features/tone/SmartFilter.h"
#include "platform/Window.h"
#include <algorithm>

namespace bb {
namespace {

LONGLONG Ticks()
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

LONGLONG Frequency()
{
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return f.QuadPart;
}

RECT PrimaryBounds()
{
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    return mi.rcMonitor;
}

// Shown and hidden from the worker thread, so nothing here waits on the window's own thread (which may be waiting for
// the worker to stop).
void Reveal(HWND w)
{
    SetLayeredWindowAttributes(w, 0, 255, LWA_ALPHA);
    SetWindowPos(w, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
}

void Conceal(HWND w) { SetLayeredWindowAttributes(w, 0, 0, LWA_ALPHA); }

} // namespace

// The window the tone-mapped picture is shown in: invisible until the first frame is ready, and hidden from capture.
class SmartFilter::Overlay : public Window {
public:
    explicit Overlay(const RECT& bounds)
    {
        Create(L"BlinkBuddy.SmartOverlay", WS_POPUP, PopupExStyle | WS_EX_LAYERED | WS_EX_TRANSPARENT, bounds,
               static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);
        excluded = SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE) != FALSE;
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    }
    bool excluded = false;

private:
    LRESULT OnMessage(UINT message, WPARAM w, LPARAM l) override
    {
        if (message == WM_ERASEBKGND) return 1;   // Direct3D owns these pixels
        return Window::OnMessage(message, w, l);
    }
};

SmartFilter::SmartFilter() = default;
SmartFilter::~SmartFilter() { Stop(); }

std::wstring SmartFilter::Status() const
{
    std::lock_guard lock(gate);
    return status;
}

void SmartFilter::SetStatus(const std::wstring& text)
{
    std::lock_guard lock(gate);
    status = text;
}

void SmartFilter::Apply(const SmartGpu::Params& p, HWND notifyWindow, UINT message)
{
    {
        std::lock_guard lock(gate);
        pending = p;
        dirty = true;
        changedAt = Ticks();
    }
    if (running || failed) return;
    notify = notifyWindow;
    failMessage = message;
    RECT bounds = PrimaryBounds();
    overlay = std::make_unique<Overlay>(bounds);
    if (!overlay->excluded) {
        overlay.reset();
        failed = true;
        SetStatus(L"Couldn't start (this Windows version can't hide the filter window from capture). Using the standard curve instead.");
        PostMessageW(notify, failMessage, 0, 0);
        return;
    }
    SetStatus(L"Starting...");
    running = true;
    HWND h = overlay->Handle();
    thread = std::thread([this, h, bounds] { Run(h, bounds); });
}

void SmartFilter::Stop()
{
    running = false;
    if (thread.joinable()) thread.join();
    overlay.reset();
}

void SmartFilter::KeepOnTop()
{
    if (running && overlay) SetWindowPos(overlay->Handle(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void SmartFilter::Fail(const std::wstring& why)
{
    failed = true;
    running = false;
    SetStatus(L"Couldn't start (" + why + L"). Using the standard curve instead.");
    PostMessageW(notify, failMessage, 0, 0);   // (the app stops this, and the window goes with it)
}

void SmartFilter::Run(HWND window, RECT bounds)
{
    SmartGpu gpu;
    std::wstring error;
    if (!gpu.Init(window, bounds, error)) {
        Fail(error);
        return;
    }
    bool shown = false, haveFrame = false;
    const LONGLONG second = Frequency();
    double refreshMs = 1000.0 / std::max(24, gpu.refreshHz), tick = second / 1000.0;
    LONGLONG frames = 0, workTicks = 0, lagTicks = 0, windowStart = Ticks();
    while (running) {
        SmartGpu::Params p;
        bool changed;
        LONGLONG changedTicks;
        {
            std::lock_guard lock(gate);
            p = pending;
            changed = dirty;
            dirty = false;
            changedTicks = changedAt;
        }
        if (changed) gpu.SetParams(p);
        // Wait long while idle; react fast for two seconds after a setting changes.
        bool adjusting = Ticks() - changedTicks < second * 2;
        LONGLONG presentedAt;
        int got = gpu.NextFrame(adjusting ? 15 : 500, presentedAt);
        if (got < 0) {
            // Lost the desktop: never leave a frozen picture up; transparent until frames flow again.
            if (shown) {
                shown = false;
                Conceal(window);
            }
            haveFrame = false;
            Sleep(250);
            gpu.Reacquire();
            continue;
        }
        if (got > 0) haveFrame = true;
        if (!haveFrame || (got == 0 && !changed)) continue;
        LONGLONG t0 = Ticks();
        gpu.Render();
        LONGLONG t1 = Ticks();
        if (!shown) {
            shown = true;
            Reveal(window);
        }
        if (got > 0) {
            frames++;
            workTicks += t1 - t0;
            // (Desktop Duplication's present time is on the same performance-counter clock.)
            if (presentedAt > 0) lagTicks += t1 - presentedAt;
        }
        if (t1 - windowStart >= second) {
            double secs = static_cast<double>(t1 - windowStart) / second;
            wchar_t text[160];
            if (frames > 0)
                swprintf_s(text, L"%.0f fps, %.1f ms of work per frame, about %.0f ms behind the screen", frames / secs, workTicks / tick / frames,
                           lagTicks / tick / frames + refreshMs);
            else
                swprintf_s(text, L"Idle: nothing on screen is changing, so nothing runs.");
            SetStatus(text);
            frames = workTicks = lagTicks = 0;
            windowStart = t1;
        }
    }
}

} // namespace bb
