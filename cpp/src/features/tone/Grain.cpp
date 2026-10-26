#include "features/tone/Grain.h"
#include "platform/Memory.h"
#include "ui/PixelSurface.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace bb {
namespace {

constexpr int Spare = 48;   // spare noise around the screen, so shifted copies still cover it
constexpr UINT_PTR JitterTimer = 1, RebuildTimer = 2;

} // namespace

class Grain::Sheet : public Window {
public:
    Sheet(const RECT& screen, int block, int maxAlpha, int tone, uint32_t seed) : screen(screen)
    {
        int w = screen.right - screen.left + 2 * Spare, h = screen.bottom - screen.top + 2 * Spare;
        Create(L"BlinkBuddy.Grain", WS_POPUP, PopupExStyle | WS_EX_LAYERED | WS_EX_TRANSPARENT,
               {screen.left - Spare, screen.top - Spare, screen.left - Spare + w, screen.top - Spare + h});
        {
            PixelSurface noise;   // (freed as soon as the compositor has it)
            noise.Ensure(w, h);
            Fill(noise, w, h, block, maxAlpha, tone, seed);
            noise.Present(hwnd, {screen.left - Spare, screen.top - Spare}, w, h);
        }
        Shift(0, 0);
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    }

    // Moves the noise by (dx, dy) while clipping it to its own screen, so it never spills onto a neighbour.
    void Shift(int dx, int dy)
    {
        int w = screen.right - screen.left, h = screen.bottom - screen.top;
        SetWindowRgn(hwnd, CreateRectRgn(Spare - dx, Spare - dy, Spare - dx + w, Spare - dy + h), FALSE);
        SetWindowPos(hwnd, HWND_TOPMOST, screen.left - Spare + dx, screen.top - Spare + dy, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    }

private:
    // Each speck changes the screen by a random amount (triangular distribution). tone 1 (mixed) lightens or
    // darkens, keeping overall brightness; 0 only darkens and 2 only lightens. (Premultiplied: a white speck at
    // alpha a is a, a, a.)
    static void Fill(PixelSurface& out, int w, int h, int block, int maxAlpha, int tone, uint32_t seed)
    {
        std::vector<uint32_t> specks((w + block - 1) / block);
        uint32_t state = seed | 1;
        for (int y = 0; y < h; y++) {
            if (y % block == 0)
                for (uint32_t& speck : specks) {
                    state ^= state << 13;   // xorshift
                    state ^= state >> 17;
                    state ^= state << 5;
                    int n = static_cast<int>(state & 255) + static_cast<int>((state >> 8) & 255) - 255;   // -255..255
                    bool light = tone == 2 || (tone == 1 && n > 0);
                    uint32_t a = static_cast<uint32_t>(std::abs(n) * maxAlpha / 255);
                    speck = a << 24 | (light ? a << 16 | a << 8 | a : 0);
                }
            uint32_t* row = out.Row(y);
            for (int x = 0; x < w; x++) row[x] = specks[x / block];
        }
    }

    RECT screen;
};

Grain::Grain() { Create(L"BlinkBuddy.GrainTimers", WS_POPUP, WS_EX_TOOLWINDOW, {0, 0, 0, 0}); }

Grain::~Grain() { Close(); }

void Grain::Apply(const Settings& s, float dpiScale, bool baked)
{
    scale = dpiScale;
    bool on = s.GrainEnabled && !baked;
    std::wstring k = on ? std::to_wstring(s.GrainStrength) + L"," + (s.GrainCoarse ? L"1," : L"0,") + std::to_wstring(s.GrainTone) : L"off";
    strength = s.GrainStrength;
    coarse = s.GrainCoarse;
    tone = s.GrainTone;
    if (k != key) {
        key = k;
        KillTimer(hwnd, RebuildTimer);
        if (!on) Close();
        else SetTimer(hwnd, RebuildTimer, 150, nullptr);   // slider drags settle before a redraw
    }
    if (on && s.GrainAnimated) SetTimer(hwnd, JitterTimer, 42, nullptr);
    else KillTimer(hwnd, JitterTimer);
}

void Grain::Build()
{
    Close();
    int block = coarse ? std::max(2, static_cast<int>(std::lround(2 * scale))) : 1;
    int maxAlpha = static_cast<int>(std::lround(strength * 1.28));   // 100% = up to half-opaque specks
    std::vector<RECT> screens;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR, HDC, RECT* r, LPARAM list) {
        reinterpret_cast<std::vector<RECT>*>(list)->push_back(*r);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&screens));
    for (const RECT& r : screens) sheets.push_back(std::make_unique<Sheet>(r, block, maxAlpha, tone, random()));
    TrimMemory();   // the noise now lives in the compositor: give back the memory used to draw it
}

void Grain::Close() { sheets.clear(); }

void Grain::Jitter()
{
    std::uniform_int_distribution<int> offset(-Spare, Spare);
    for (auto& sheet : sheets) sheet->Shift(offset(random), offset(random));
}

LRESULT Grain::OnMessage(UINT message, WPARAM w, LPARAM l)
{
    if (message == WM_TIMER) {
        if (w == RebuildTimer) {
            KillTimer(hwnd, RebuildTimer);
            Build();
        } else if (w == JitterTimer) Jitter();
        return 0;
    }
    return Window::OnMessage(message, w, l);
}

} // namespace bb
