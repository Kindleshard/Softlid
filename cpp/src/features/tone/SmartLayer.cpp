#include "features/tone/SmartLayer.h"
#include "core/ToneCurve.h"
#include "features/tone/SmartGpu.h"
#include "platform/Window.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace bb {
namespace {

constexpr int LookEveryMs = 200;
constexpr int CellLevel = 2;   // brightness map cells are 2^(level+1) = 8 screen pixels

LONGLONG Ticks()
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

// Brightness-similarity weight exp(-d^2/2) for a difference of d stops, in 1/64-stop steps up to 8 stops (beyond that
// it is effectively zero), so the 25-tap blur needs no exp() per tap.
const std::vector<float>& Similar()
{
    static const std::vector<float> table = [] {
        std::vector<float> t(8 * 64 + 1);
        for (size_t i = 0; i < t.size(); i++) t[i] = static_cast<float>(std::exp(-0.5 * (i / 64.0) * (i / 64.0)));
        return t;
    }();
    return table;
}

// Per-cell layer opacity: each cell's background is a brightness-aware blur of its neighbours (so bright and dark
// regions don't bleed into each other), remapped exactly like Smart (GPU), and the cell is dimmed by the same factor.
// Then the peak rule: no cell's brightest pixel may end up brighter than a calmed white page, which catches thin white
// text and small bright icons that the average hides. Opacity is in the screen's encoded scale, where the layer blends.
std::vector<float> Mask(const std::vector<float>& map, const std::vector<float>& peaks, int w, int h, const SmartLayer::Params& p)
{
    std::vector<float> l(map.size()), a(map.size());
    double undo = std::pow(std::max(0.05f, p.gain), 2.2);
    double brightest = ToneCurve::Remap(p.level, 0);   // where a white page ends up, in log2
    for (size_t i = 0; i < map.size(); i++) l[i] = static_cast<float>(std::log2(std::max(map[i] / undo, 1e-4)));
    float spatial[25];
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++) spatial[(dy + 2) * 5 + dx + 2] = static_cast<float>(std::exp(-(dx * dx + dy * dy) / 4.5));
    const std::vector<float>& similar = Similar();
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float self = l[static_cast<size_t>(y) * w + x], sum = 0, weights = 0;
            for (int dy = -2; dy <= 2; dy++) {
                size_t row = static_cast<size_t>(std::clamp(y + dy, 0, h - 1)) * w;
                for (int dx = -2; dx <= 2; dx++) {
                    float v = l[row + std::clamp(x + dx, 0, w - 1)];
                    auto d = static_cast<size_t>(std::abs(v - self) * 64);
                    float wgt = spatial[(dy + 2) * 5 + dx + 2] * (d < similar.size() ? similar[d] : 0) + 1e-3f;
                    sum += v * wgt;
                    weights += wgt;
                }
            }
            double b = sum / weights;
            double lk = ToneCurve::Remap(p.level, b) - b;   // log2 of the linear-light factor, at most 0
            double peak = std::log2(std::max(peaks[static_cast<size_t>(y) * w + x] / undo, 1e-4));
            lk = std::min(lk, brightest - peak);
            a[static_cast<size_t>(y) * w + x] = static_cast<float>(1 - std::pow(2.0, lk / 2.2));
        }
    return a;
}

// A 256x256 tile of grain specks (-255..255), the same distribution as the standalone grain.
std::vector<int> Noise(const SmartLayer::Params& p)
{
    std::vector<int> n(256 * 256);
    uint32_t state = 0x9E3779B9;
    for (int& v : n) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        int s = static_cast<int>(state & 255) + static_cast<int>((state >> 8) & 255) - 255;
        v = (std::abs(s) * p.grainAlpha / 255) * (p.grainTone == 2 || (p.grainTone == 1 && s > 0) ? 1 : -1);
    }
    return n;
}

// The bounding box of the cells whose dimming moved by more than ~2.5/255, grown by one cell for the smoothing between
// neighbours; empty when nothing visible changed.
RECT ChangedCells(const std::vector<float>& next, const std::vector<float>& shown, int w, int h)
{
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (std::abs(next[static_cast<size_t>(y) * w + x] - shown[static_cast<size_t>(y) * w + x]) > 0.01f) {
                x0 = std::min(x0, x);
                x1 = std::max(x1, x);
                y0 = std::min(y0, y);
                y1 = std::max(y1, y);
            }
    if (x1 < 0) return {};
    return {std::max(0, x0 - 1), std::max(0, y0 - 1), std::min(w - 1, x1 + 1) + 1, std::min(h - 1, y1 + 1) + 1};
}

// The layer's pixels: a top-down premultiplied bitmap that lives as long as the layer, drawn into directly and handed
// to the compositor with only the part that changed.
class Surface {
public:
    Surface(HWND window, const RECT& bounds) : window(window), at{bounds.left, bounds.top}, w(bounds.right - bounds.left), h(bounds.bottom - bounds.top)
    {
        BITMAPINFO bi{};
        bi.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
        dc = CreateCompatibleDC(nullptr);
        void* p = nullptr;
        bitmap = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &p, nullptr, 0);
        bits = static_cast<uint32_t*>(p);
        old = SelectObject(dc, bitmap);
    }
    ~Surface()
    {
        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
    }

    // Upscales the cell mask smoothly (bilinear between cell centres) over the pixels those cells influence, adding
    // grain. Black at opacity a multiplies what's underneath by (1 - a); a light speck is white over it.
    RECT Draw(const std::vector<float>& mask, int mw, int mh, RECT cells, const std::vector<int>* noise, int block)
    {
        if (!bits) return {};
        float cellW = static_cast<float>(w) / mw, cellH = static_cast<float>(h) / mh;
        RECT area{static_cast<LONG>(cells.left * cellW), static_cast<LONG>(cells.top * cellH), static_cast<LONG>(std::ceil(cells.right * cellW)),
                  static_cast<LONG>(std::ceil(cells.bottom * cellH))};
        RECT all{0, 0, w, h};
        IntersectRect(&area, &area, &all);
        if (first) area = all;
        block = std::max(1, block);
        std::vector<float> rowMask(mw);
        for (int y = area.top; y < area.bottom; y++) {
            float cy = std::clamp((y + 0.5f) / cellH - 0.5f, 0.0f, mh - 1.001f);
            int y0 = static_cast<int>(cy);
            float fy = cy - y0;
            for (int i = 0; i < mw; i++) rowMask[i] = mask[static_cast<size_t>(y0) * mw + i] * (1 - fy) + mask[static_cast<size_t>(y0 + 1) * mw + i] * fy;
            int noiseRow = (y / block & 255) * 256;
            uint32_t* row = bits + static_cast<size_t>(y) * w;
            for (int x = area.left; x < area.right; x++) {
                float cx = std::clamp((x + 0.5f) / cellW - 0.5f, 0.0f, mw - 1.001f);
                int x0 = static_cast<int>(cx);
                float fx = cx - x0;
                auto dim = static_cast<uint32_t>((rowMask[x0] * (1 - fx) + rowMask[x0 + 1] * fx) * 255 + 0.5f);
                uint32_t px;
                if (!noise) {
                    px = dim << 24;
                } else {
                    int g = (*noise)[noiseRow + (x / block & 255)];
                    if (g <= 0) px = (dim + static_cast<uint32_t>(-g) * (255 - dim) / 255) << 24;   // a darker speck: more black
                    else {
                        auto ug = static_cast<uint32_t>(g);
                        px = ((ug + dim * (255 - ug) / 255) << 24) | (ug << 16) | (ug << 8) | ug;   // a white speck, premultiplied
                    }
                }
                row[x] = px;
            }
        }
        return area;
    }

    void Present(const RECT& area)
    {
        SIZE size{w, h};
        POINT from{};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UPDATELAYEREDWINDOWINFO info{sizeof info, nullptr, &at, &size, dc, &from, 0, &blend, ULW_ALPHA, first ? nullptr : &area};
        UpdateLayeredWindowIndirect(window, &info);
        first = false;
    }

private:
    HWND window;
    POINT at;
    int w, h;
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ old = nullptr;
    uint32_t* bits = nullptr;
    bool first = true;
};

} // namespace

class SmartLayer::Layer : public Window {
public:
    explicit Layer(const RECT& bounds)
    {
        Create(L"Softlid.SmartLayer", WS_POPUP, PopupExStyle | WS_EX_LAYERED | WS_EX_TRANSPARENT, bounds);
        // Hidden from capture, so measuring the screen never sees the layer's own dimming.
        excluded = SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE) != FALSE;
    }
    bool excluded = false;
};

SmartLayer::SmartLayer() = default;
SmartLayer::~SmartLayer() { Stop(); }

std::wstring SmartLayer::Status() const
{
    std::lock_guard lock(gate);
    return status;
}

void SmartLayer::SetStatus(const std::wstring& text)
{
    std::lock_guard lock(gate);
    status = text;
}

void SmartLayer::Apply(const Params& p, HWND notifyWindow, UINT message)
{
    {
        std::lock_guard lock(gate);
        pending = p;
        dirty = true;
    }
    if (running || failed) return;
    notify = notifyWindow;
    failMessage = message;
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    RECT bounds = mi.rcMonitor;
    layer = std::make_unique<Layer>(bounds);
    if (!layer->excluded) {
        layer.reset();
        failed = true;
        SetStatus(L"Couldn't start (this Windows version can't hide the layer from capture). Using the standard curve instead.");
        PostMessageW(notify, failMessage, 0, 0);
        return;
    }
    SetStatus(L"Starting...");
    running = true;
    HWND h = layer->Handle();
    thread = std::thread([this, h, bounds] { Run(h, bounds); });
}

void SmartLayer::Stop()
{
    running = false;
    if (thread.joinable()) thread.join();
    layer.reset();
}

void SmartLayer::KeepOnTop()
{
    if (running && layer && IsWindowVisible(layer->Handle()))
        SetWindowPos(layer->Handle(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void SmartLayer::Fail(const std::wstring& why)
{
    failed = true;
    running = false;
    SetStatus(L"Couldn't start (" + why + L"). Using the standard curve instead.");
    PostMessageW(notify, failMessage, 0, 0);
}

void SmartLayer::Run(HWND window, RECT bounds)
{
    SmartGpu gpu;
    std::wstring error;
    if (!gpu.Init(nullptr, bounds, error)) {
        Fail(error);
        return;
    }
    Surface surface(window, bounds);
    std::vector<float> map, peaks, shownMask;
    int mw = 0, mh = 0;
    std::vector<int> noise;
    std::wstring noiseKey = L"-";
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    const LONGLONG second = f.QuadPart;
    LONGLONG screenPixels = static_cast<LONGLONG>(bounds.right - bounds.left) * (bounds.bottom - bounds.top), pendingChange = 0, lastLook = 0;
    LONGLONG windowStart = Ticks(), workTicks = 0, drawnPixels = 0;
    int looks = 0, redraws = 0;
    bool shown = false;
    while (running) {
        Params p;
        bool changed;
        {
            std::lock_guard lock(gate);
            p = pending;
            changed = dirty;
            dirty = false;
        }
        LONGLONG presentedAt, changedPixels;
        int got = gpu.NextFrame(0, presentedAt, &changedPixels);   // never waits: whatever changed since the last look
        if (got < 0) {
            gpu.Reacquire();
            Sleep(250);
            continue;
        }
        LONGLONG t0 = Ticks();
        if (got > 0) pendingChange += changedPixels;
        // Measure again once at least 1% of the screen changed, or after 2 s if anything changed at all: a blinking
        // cursor or a ticking clock never costs a look.
        bool look = got > 0 && (map.empty() || pendingChange >= screenPixels / 100 || (pendingChange > 0 && t0 - lastLook > second * 2));
        if (look) {
            map = gpu.Probe(CellLevel, mw, mh);
            peaks = gpu.ProbePeaks(mw, mh);
            pendingChange = 0;
            lastLook = t0;
            looks++;
        }
        std::wstring key = p.grain ? std::to_wstring(p.grainAlpha) + L"," + std::to_wstring(p.grainTone) + L"," + std::to_wstring(p.grainBlock) : L"";
        if (key != noiseKey) {
            noiseKey = key;
            noise = p.grain ? Noise(p) : std::vector<int>{};
            changed = true;
        }
        if (!map.empty() && (look || changed)) {
            std::vector<float> next = Mask(map, peaks, mw, mh, p);
            RECT cells = changed || shownMask.empty() ? RECT{0, 0, mw, mh} : ChangedCells(next, shownMask, mw, mh);
            if (cells.right > cells.left) {
                if (shownMask.empty()) shownMask.assign(next.size(), 0.0f);
                for (int y = cells.top; y < cells.bottom; y++)
                    for (int x = cells.left; x < cells.right; x++) shownMask[static_cast<size_t>(y) * mw + x] = next[static_cast<size_t>(y) * mw + x];
                RECT area = surface.Draw(shownMask, mw, mh, cells, p.grain ? &noise : nullptr, p.grainBlock);
                surface.Present(area);
                drawnPixels += static_cast<LONGLONG>(area.right - area.left) * (area.bottom - area.top);
                if (!shown) {
                    shown = true;
                    ShowWindowAsync(window, SW_SHOWNOACTIVATE);
                }
                redraws++;
            }
        }
        workTicks += Ticks() - t0;
        LONGLONG now = Ticks();
        if (now - windowStart >= second * 5) {
            double secs = static_cast<double>(now - windowStart) / second;
            wchar_t text[200];
            swprintf_s(text, L"looked %.1fx/s, redrew %.1fx/s (%.0f%% of the screen each); %.1f ms of work per second, no delay", looks / secs,
                       redraws / secs, redraws > 0 ? 100.0 * drawnPixels / redraws / screenPixels : 0.0, workTicks * 1000.0 / second / secs);
            SetStatus(text);
            looks = redraws = 0;
            workTicks = drawnPixels = 0;
            windowStart = now;
        }
        Sleep(LookEveryMs);
    }
}

} // namespace bb
