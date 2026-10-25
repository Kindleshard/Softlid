#include "ui/ScreenShade.h"
#include <algorithm>

namespace bb {

class ScreenShade::Sheet : public Window {
public:
    explicit Sheet(const RECT& monitor)
    {
        Create(L"BlinkBuddy.Shade", WS_POPUP, PopupExStyle | WS_EX_LAYERED | WS_EX_TRANSPARENT, monitor,
               static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    }
    void SetDarkness(float d) { SetLayeredWindowAttributes(hwnd, 0, static_cast<BYTE>(std::clamp(d, 0.0f, 1.0f) * 255), LWA_ALPHA); }
};

ScreenShade::ScreenShade() = default;
ScreenShade::~ScreenShade() = default;

void ScreenShade::Show(float darkness)
{
    // One sheet per monitor, made again if the monitors changed since last time.
    std::vector<RECT> monitors;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR, HDC, LPRECT r, LPARAM list) {
        reinterpret_cast<std::vector<RECT>*>(list)->push_back(*r);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&monitors));
    if (monitors.size() != sheets.size()) {
        sheets.clear();
        for (const RECT& m : monitors) sheets.push_back(std::make_unique<Sheet>(m));
    }
    SetDarkness(darkness);
    for (auto& sheet : sheets) ShowOnTop(sheet->Handle());
    showing = true;
}

void ScreenShade::SetDarkness(float darkness)
{
    for (auto& sheet : sheets) sheet->SetDarkness(darkness);
}

void ScreenShade::Hide()
{
    for (auto& sheet : sheets) ShowWindow(sheet->Handle(), SW_HIDE);
    showing = false;
}

} // namespace bb
