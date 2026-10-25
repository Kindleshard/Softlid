#include "features/strip/ReadingStrip.h"
#include "platform/Activity.h"
#include "platform/Window.h"
#include "ui/Theme.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace bb {
namespace {

constexpr DWORD SheetStyle = PopupExStyle | WS_EX_LAYERED | WS_EX_TRANSPARENT;   // on top, click-through, never focused

COLORREF Gdi(int argb) { return RGB((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF); }

RECT MonitorOf(POINT p)
{
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST), &mi);
    return mi.rcMonitor;
}

bool Inside(const RECT& r, POINT p) { return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom; }

// The rounded rectangle's corner radius, for half-sizes hx and hy.
double Corner(double hx, double hy) { return std::min(std::min(hx, hy), 6 + std::min(hx, hy) * 0.08); }

// How far outside the clear area a pixel is (negative inside), for each shape; pad is the room round it.
double Distance(int shape, double x, double y, int w, int h, int pad)
{
    double cx = w / 2.0, cy = h / 2.0;
    if (shape == 0) return std::abs(y - cy) - (cy - pad);
    if (shape == 1) return std::abs(x - cx) - (cx - pad);
    if (shape == 3) return std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy)) - (cx - pad);
    double hx = cx - pad, hy = cy - pad, r = Corner(hx, hy);   // a softly rounded rectangle
    double qx = std::abs(x - cx) - (hx - r), qy = std::abs(y - cy) - (hy - r);
    double ox = std::max(qx, 0.0), oy = std::max(qy, 0.0);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0) - r;
}

} // namespace

// The tinted sheet: one solid colour, see-through by its opacity, with the clear area cut out of its region.
class ReadingStrip::Sheet : public Window {
public:
    Sheet() { Create(L"BlinkBuddy.StripSheet", WS_POPUP, SheetStyle, {0, 0, 1, 1}); }

    // Over the monitor, in the colour and opacity given (each change repaints all of it).
    void Set(const RECT& monitor, int argb, BYTE alpha)
    {
        if (EqualRect(&monitor, &bounds) && argb == color && alpha == opacity) return;
        bounds = monitor;
        color = argb;
        opacity = alpha;
        SetWindowRgn(hwnd, nullptr, TRUE);   // all of it in the new colour, not just what shows now
        cut.clear();
        SetLayeredWindowAttributes(hwnd, 0, std::max<BYTE>(3, alpha), LWA_ALPHA);   // (kept above 0, so it stays layered)
        SetWindowPos(hwnd, HWND_TOPMOST, monitor.left, monitor.top, monitor.right - monitor.left, monitor.bottom - monitor.top,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(hwnd, nullptr, TRUE);
        UpdateWindow(hwnd);
    }

    // Shows only `keep` minus a hole (window coordinates): 0 a rectangle, 1 a rounded rectangle, 2 an ellipse. One
    // call, so the window changes in a single step.
    void Cut(const RECT& keep, const RECT& hole, int kind, int radius)
    {
        std::wstring key = std::to_wstring(keep.left) + L"," + std::to_wstring(keep.top) + L"," + std::to_wstring(keep.right) + L"," +
                           std::to_wstring(keep.bottom) + L"|" + std::to_wstring(hole.left) + L"," + std::to_wstring(hole.top) + L"," +
                           std::to_wstring(hole.right) + L"," + std::to_wstring(hole.bottom) + L"|" + std::to_wstring(kind) + L"," +
                           std::to_wstring(radius);
        if (key == cut) return;
        cut = key;
        HRGN r = CreateRectRgn(keep.left, keep.top, keep.right, keep.bottom);
        if (hole.right > hole.left && hole.bottom > hole.top) {
            HRGN h = kind == 2 ? CreateEllipticRgn(hole.left, hole.top, hole.right + 1, hole.bottom + 1)
                   : kind == 1 ? CreateRoundRectRgn(hole.left, hole.top, hole.right + 1, hole.bottom + 1, radius * 2, radius * 2)
                               : CreateRectRgn(hole.left, hole.top, hole.right, hole.bottom);
            CombineRgn(r, r, h, RGN_DIFF);
            DeleteObject(h);
        }
        SetWindowRgn(hwnd, r, TRUE);   // (Windows owns the region from here)
    }

    void Hide() { ShowWindow(hwnd, SW_HIDE); bounds = {}; }

private:
    LRESULT OnMessage(UINT message, WPARAM w, LPARAM l) override
    {
        if (message == WM_ERASEBKGND) {
            RECT r;
            GetClientRect(hwnd, &r);
            HBRUSH brush = CreateSolidBrush(Gdi(color));
            FillRect(reinterpret_cast<HDC>(w), &r, brush);
            DeleteObject(brush);
            return 1;
        }
        return Window::OnMessage(message, w, l);
    }

    RECT bounds{};
    int color = 0;
    BYTE opacity = 0;
    std::wstring cut;
};

// The edge round the clear area (soft, or an outline), dimmed around it, as one per-pixel-alpha window.
class ReadingStrip::Tile : public Window {
public:
    Tile() { Create(L"BlinkBuddy.StripTile", WS_POPUP, SheetStyle, {0, 0, 1, 1}); }

    // Draws it at w x h (only when how it looks changed: it is a loop over every pixel).
    void Draw(int w, int h, int shape, int pad, int feather, int alpha, int tint, int border, int cover, int line, POINT where)
    {
        std::wstring look = std::to_wstring(w) + L"x" + std::to_wstring(h);
        for (int v : {shape, pad, feather, alpha, tint, border, cover, line}) look += L"," + std::to_wstring(v);
        if (look == drawn) return;
        drawn = look;
        w = std::max(1, w);
        h = std::max(1, h);
        BITMAPINFO bi{};
        bi.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
        void* bits = nullptr;
        HDC screenDc = GetDC(nullptr), dc = CreateCompatibleDC(screenDc);
        HBITMAP bitmap = CreateDIBSection(screenDc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bitmap) {
            DeleteDC(dc);
            ReleaseDC(nullptr, screenDc);
            return;
        }
        auto* px = static_cast<UINT32*>(bits);
        double tr = (tint >> 16) & 0xFF, tg = (tint >> 8) & 0xFF, tb = tint & 0xFF;
        double lr = (line >> 16) & 0xFF, lg = (line >> 8) & 0xFF, lb = line & 0xFF;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                if (shape == 0 && x > 0) {   // a band's rows are all alike across: work out the first pixel, copy it on
                    px[y * w + x] = px[y * w];
                    continue;
                }
                double d = Distance(shape, x + 0.5, y + 0.5, w, h, pad);
                bool before = shape == 0 ? y < h / 2 : x < w / 2;
                double a;
                if (feather > 0) {
                    a = d <= 0 ? 0 : std::min(1.0, d / feather);
                    a = a * a * (3 - 2 * a);   // smoothstep: no visible start or end
                } else {
                    a = std::clamp(d + 0.5, 0.0, 1.0);   // a crisp, anti-aliased edge
                }
                a *= alpha;
                if ((cover == 1 && !before) || (cover == 2 && before)) a = 0;
                double r = tr * a / 255, g = tg * a / 255, b = tb * a / 255;
                if (border > 0 && d > -0.5 && d <= border + 0.5) {
                    double k = std::min(1.0, std::min(d + 0.5, border + 0.5 - d)) * 0.9;   // an anti-aliased line
                    a += (255 - a) * k;
                    r += (lr - r) * k;
                    g += (lg - g) * k;
                    b += (lb - b) * k;
                }
                px[y * w + x] = (static_cast<UINT32>(a) << 24) | (static_cast<UINT32>(r) << 16) | (static_cast<UINT32>(g) << 8) | static_cast<UINT32>(b);
            }
        }
        HGDIOBJ old = SelectObject(dc, bitmap);
        SIZE size{w, h};
        POINT zero{};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(hwnd, screenDc, &where, &size, dc, &zero, 0, &blend, ULW_ALPHA);
        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
        ReleaseDC(nullptr, screenDc);
        clipped = {-1, -1, -1, -1};   // a new size: clip it again
    }

    // Moves it to t, clipped to its screen so it never spills onto a neighbour.
    void Place(const RECT& t, const RECT& monitor)
    {
        RECT clip;
        IntersectRect(&clip, &t, &monitor);
        OffsetRect(&clip, -t.left, -t.top);
        if (!EqualRect(&clip, &clipped)) {
            clipped = clip;
            SetWindowRgn(hwnd, CreateRectRgn(clip.left, clip.top, clip.right, clip.bottom), TRUE);
        }
        SetWindowPos(hwnd, HWND_TOPMOST, t.left, t.top, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

    void Hide()
    {
        ShowWindow(hwnd, SW_HIDE);
        drawn.clear();
    }

private:
    std::wstring drawn;
    RECT clipped{-1, -1, -1, -1};
};

ReadingStrip::ReadingStrip() = default;
ReadingStrip::~ReadingStrip() { Stop(); }

void ReadingStrip::Apply(const Settings& settings, HWND listener)
{
    s = &settings;
    if (!settings.StripEnabled) {
        Listen(listener, false);
        Stop();
        return;
    }
    if (!dim) {
        dim = std::make_unique<Sheet>();
        tile = std::make_unique<Tile>();
    }
    Listen(listener, settings.StripFollow);
    if (settings.StripFollow || !placed) GetCursorPos(&at);
    if (!placed || !settings.StripOneMonitor || IsRectEmpty(&screen)) screen = MonitorOf(at);
    scale = ScaleAt(at);
    placed = true;
    Place();
}

void ReadingStrip::Listen(HWND listener, bool on)
{
    if (on == listening) return;
    RAWINPUTDEVICE mouse{0x01, 0x02, on ? static_cast<DWORD>(RIDEV_INPUTSINK) : static_cast<DWORD>(RIDEV_REMOVE), on ? listener : nullptr};
    if (RegisterRawInputDevices(&mouse, 1, sizeof mouse)) listening = on;
}

void ReadingStrip::MouseMoved()
{
    if (!placed || !s || !s->StripFollow) return;
    POINT p;
    GetCursorPos(&p);
    if (p.x == at.x && p.y == at.y) return;
    if (!s->StripOneMonitor && !Inside(screen, p)) screen = MonitorOf(p);
    at = p;
    Place();
}

void ReadingStrip::Nudge(int dx, int dy)
{
    if (!placed || !s || !s->StripEnabled) return;
    ULONGLONG now = GetTickCount64();
    double step = 3 * scale;
    speed = now - lastNudge < 220 && s->StripAccel > 0 ? std::min(90.0 * scale, std::max(step, speed) * (1 + s->StripAccel / 100.0 * 0.45))
                                                       : step;
    lastNudge = now;
    at.x = std::clamp(at.x + static_cast<LONG>(std::lround(dx * speed)), screen.left, screen.right - 1);
    at.y = std::clamp(at.y + static_cast<LONG>(std::lround(dy * speed)), screen.top, screen.bottom - 1);
    Place();
}

void ReadingStrip::Place()
{
    const Settings& st = *s;
    RECT sc = screen;
    int shape = st.StripShape, edge = st.StripEdge;
    int h = static_cast<int>(st.StripHeight * scale), w = static_cast<int>(st.StripWidth * scale);
    int feather = edge == 1 ? std::max(1, static_cast<int>(st.StripFeather * scale)) : 0;
    int border = edge == 2 ? std::max(1, static_cast<int>(std::lround(st.StripBorder * scale))) : 0;
    int pad = edge == 1 ? feather : edge == 2 ? border + 2 : 0;   // the edge sits just outside the clear area
    int alpha = static_cast<int>(std::lround(st.StripDim * 2.55)), cover = shape <= 1 ? st.StripCover : 0;
    int tint = st.StripColor | 0xFF000000, line = st.StripLine == 0 ? Theme::Accent() : st.StripLine | 0xFF000000;
    int cx = std::clamp(at.x, sc.left, sc.right - 1), cy = std::clamp(at.y, sc.top, sc.bottom - 1);
    if (shape == 3) w = h;
    // The clear area with its edge round it.
    RECT t = shape == 0 ? RECT{sc.left, cy - h / 2 - pad, sc.right, cy - h / 2 - pad + h + 2 * pad}
           : shape == 1 ? RECT{cx - w / 2 - pad, sc.top, cx - w / 2 - pad + w + 2 * pad, sc.bottom}
                        : RECT{cx - w / 2 - pad, cy - h / 2 - pad, cx - w / 2 - pad + w + 2 * pad, cy - h / 2 - pad + h + 2 * pad};

    // The edge first, so the hole never opens where it isn't yet.
    if (edge == 0) tile->Hide();
    else {
        tile->Draw(t.right - t.left, t.bottom - t.top, shape, pad, feather, alpha, tint, border, cover, line, {t.left, t.top});
        tile->Place(t, sc);
    }

    // The sheet: the whole monitor (or, for a band dimming one side, just that side), minus the hole. Without an
    // edge the hole has the clear area's own shape; with one it is the edge window's rectangle.
    dim->Set(sc, tint, static_cast<BYTE>(alpha));
    int sw = sc.right - sc.left, sh = sc.bottom - sc.top;
    RECT keep{0, 0, sw, sh};
    if (shape == 0 && cover == 1) keep = {0, 0, sw, t.top - sc.top};
    if (shape == 0 && cover == 2) keep = {0, t.bottom - sc.top, sw, sh};
    if (shape == 1 && cover == 1) keep = {0, 0, t.left - sc.left, sh};
    if (shape == 1 && cover == 2) keep = {t.right - sc.left, 0, sw, sh};
    RECT hole{t.left - sc.left, t.top - sc.top, t.right - sc.left, t.bottom - sc.top};
    int kind = edge != 0 ? 0 : shape == 3 ? 2 : shape == 2 ? 1 : 0;
    dim->Cut(keep, hole, kind, static_cast<int>(Corner(w / 2.0, h / 2.0)));
}

void ReadingStrip::Stop()
{
    placed = false;
    if (!dim) return;
    tile->Hide();
    dim->Hide();
}

} // namespace bb
