#include "platform/Location.h"
#include "platform/Window.h"
#include <winrt/Windows.Devices.Geolocation.h>
#include <winrt/Windows.Foundation.h>
#include <cmath>

#pragma comment(lib, "windowsapp")

namespace bb::Location {
namespace {

struct Request { HWND window; UINT message; };

// Whether Windows' location switches let desktop apps ask at all: for the PC, for this user, and for desktop apps
// (Settings > Privacy > Location). If one is off, asking would only wait until it times out.
bool LocationAllowed()
{
    auto denied = [](HKEY root, const wchar_t* key) {
        wchar_t value[16] = {};
        DWORD size = sizeof value;
        return RegGetValueW(root, key, L"Value", RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_SUCCESS && _wcsicmp(value, L"Deny") == 0;
    };
    constexpr wchar_t store[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\location";
    constexpr wchar_t desktop[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\location\\NonPackaged";
    return !denied(HKEY_LOCAL_MACHINE, store) && !denied(HKEY_CURRENT_USER, store) && !denied(HKEY_CURRENT_USER, desktop);
}

DWORD WINAPI Work(void* param)
{
    Request request = *static_cast<Request*>(param);
    delete static_cast<Request*>(param);
    Fix fix{false, 0, 0};
    if (!LocationAllowed()) {
        PostOwned(request.window, request.message, 0, fix);
        return 0;
    }
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);   // (a worker thread may block on the answer)
        using namespace winrt::Windows::Devices::Geolocation;
        Geolocator locator;
        locator.DesiredAccuracy(PositionAccuracy::Default);
        using std::chrono::minutes, std::chrono::seconds;   // (an answer up to an hour old will do; wait 45 s at most)
        BasicGeoposition at = locator.GetGeopositionAsync(minutes(60), seconds(45)).get().Coordinate().Point().Position();
        fix.lat = std::round(at.Latitude * 100) / 100;
        fix.lon = std::round(at.Longitude * 100) / 100;
        fix.ok = true;
    } catch (...) {
        // Location off, denied, or no answer: the time zone's city stands in.
    }
    winrt::uninit_apartment();
    PostOwned(request.window, request.message, 0, fix);
    return 0;
}

} // namespace

void Locate(HWND window, UINT message)
{
    auto* request = new Request{window, message};
    HANDLE thread = CreateThread(nullptr, 0, Work, request, 0, nullptr);
    if (thread) CloseHandle(thread);
    else {
        delete request;
        PostOwned(window, message, 0, Fix{false, 0, 0});
    }
}

} // namespace bb::Location
