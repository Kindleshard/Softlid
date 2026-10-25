#include "features/tone/GammaRamp.h"
#include "core/ToneCurve.h"
#include "platform/Files.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace bb {
namespace {

constexpr double MaxDeviation = 0.45;   // a little inside Windows' limit of 0.5

// Exists while one of our ramps is on screen, so a crash can be cleaned up on the next start.
std::wstring Marker() { return RoamingAppData() + L"\\BlinkBuddy\\tone-active"; }

std::vector<std::wstring> Displays()
{
    std::vector<std::wstring> names;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR m, HDC, LPRECT, LPARAM p) {
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof mi;
        if (GetMonitorInfoW(m, &mi)) reinterpret_cast<std::vector<std::wstring>*>(p)->push_back(mi.szDevice);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&names));
    return names;
}

double WorstDeviation(double d, const std::function<double(double)>& f)
{
    double worst = 0;
    for (int i = 0; i < 256; i++) {
        double y = i / 255.0;
        worst = std::max(worst, std::abs(ToneCurve::Lookup(y, d, f) - y));
    }
    return worst;
}

std::array<WORD, 768> Identity()
{
    std::array<WORD, 768> ramp{};
    for (int c = 0; c < 3; c++)
        for (int i = 0; i < 256; i++) ramp[c * 256 + i] = static_cast<WORD>(i * 257);
    return ramp;
}

} // namespace

void GammaRamp::Off()
{
    Restore();
    prescale = 1;
    refused = false;
    status.clear();
}

void GammaRamp::SetCurve(const std::function<double(double)>& f)
{
    // The least pre-scaling (best precision in dark gradients) whose ramp stays inside the limit; if Windows still
    // refuses, retry with a tighter limit.
    for (double allowed : {MaxDeviation, 0.4, 0.35, 0.3}) {
        double d = 1;
        while (d > 0.15 && WorstDeviation(d, f) > allowed) d -= 0.01;
        if (WorstDeviation(d, f) > allowed || !TryApply(d, f)) continue;
        prescale = static_cast<float>(d);
        refused = false;
        wchar_t text[160];
        swprintf_s(text, L"10%% → %.0f%%    25%% → %.0f%%    50%% → %.0f%%    white → %.0f%%", f(0.1) * 100, f(0.25) * 100,
                   f(0.5) * 100, f(1) * 100);
        status = text;
        return;
    }
    Restore();
    prescale = 1;
    refused = true;
    status = L"Windows refused the curve on this display (HDR may be on).";
}

void GammaRamp::Verify()
{
    Ramp current{};
    for (const std::wstring& device : Displays()) {
        auto mine = applied.find(device);
        if (mine == applied.end()) continue;
        HDC dc = CreateDCW(device.c_str(), nullptr, nullptr, nullptr);
        if (!dc) continue;
        if (GetDeviceGammaRamp(dc, current.data()) &&
            (current[255] != mine->second[255] || current[128] != mine->second[128] || current[32] != mine->second[32]))
            SetDeviceGammaRamp(dc, mine->second.data());
        DeleteDC(dc);
    }
}

void GammaRamp::Reset()
{
    Restore();
    originals.clear();
    Off();
}

void GammaRamp::ClearLeftover()
{
    if (GetFileAttributesW(Marker().c_str()) == INVALID_FILE_ATTRIBUTES) return;
    Ramp identity = Identity();
    for (const std::wstring& device : Displays()) {
        HDC dc = CreateDCW(device.c_str(), nullptr, nullptr, nullptr);
        if (!dc) continue;
        SetDeviceGammaRamp(dc, identity.data());
        DeleteDC(dc);
    }
    DeleteFileW(Marker().c_str());
}

bool GammaRamp::TryApply(double d, const std::function<double(double)>& f)
{
    bool ok = true;
    for (const std::wstring& device : Displays()) {
        HDC dc = CreateDCW(device.c_str(), nullptr, nullptr, nullptr);
        if (!dc) continue;
        const Ramp& base = Original(device, dc);
        Ramp ramp{};
        for (int c = 0; c < 3; c++)
            for (int i = 0; i < 256; i++) {
                // Our curve first, then the display's own calibration ramp.
                double pos = ToneCurve::Lookup(i / 255.0, d, f) * 255;
                int lo = std::clamp(static_cast<int>(pos), 0, 255), hi = std::min(255, lo + 1);
                double t = pos - lo;
                ramp[c * 256 + i] = static_cast<WORD>(std::lround(base[c * 256 + lo] * (1 - t) + base[c * 256 + hi] * t));
            }
        if (SetDeviceGammaRamp(dc, ramp.data())) applied[device] = ramp;
        else ok = false;
        DeleteDC(dc);
    }
    if (ok) {
        CreateDirectoryW((RoamingAppData() + L"\\BlinkBuddy").c_str(), nullptr);
        HANDLE marker = CreateFileW(Marker().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (marker != INVALID_HANDLE_VALUE) CloseHandle(marker);
    }
    return ok;
}

const GammaRamp::Ramp& GammaRamp::Original(const std::wstring& device, HDC dc)
{
    auto found = originals.find(device);
    if (found != originals.end()) return found->second;
    Ramp ramp{};
    if (!GetDeviceGammaRamp(dc, ramp.data())) ramp = Identity();
    return originals[device] = ramp;
}

void GammaRamp::Restore()
{
    for (auto& [device, ramp] : originals) {
        if (!applied.count(device)) continue;
        HDC dc = CreateDCW(device.c_str(), nullptr, nullptr, nullptr);
        if (!dc) continue;
        SetDeviceGammaRamp(dc, ramp.data());
        DeleteDC(dc);
    }
    applied.clear();
    DeleteFileW(Marker().c_str());
}

} // namespace bb
