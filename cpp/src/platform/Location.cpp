#include "platform/Location.h"
#include <winrt/Windows.Devices.Geolocation.h>
#include <winrt/Windows.Foundation.h>
#include <cmath>

#pragma comment(lib, "windowsapp")

namespace bb::Location {
namespace {

struct Request { HWND window; UINT message; };

DWORD WINAPI Work(void* param)
{
    Request request = *static_cast<Request*>(param);
    delete static_cast<Request*>(param);
    auto* fix = new Fix{false, 0, 0};
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);   // (a worker thread may block on the answer)
        using namespace winrt::Windows::Devices::Geolocation;
        Geolocator locator;
        locator.DesiredAccuracy(PositionAccuracy::Default);
        using std::chrono::minutes, std::chrono::seconds;   // (an answer up to an hour old will do; wait 45 s at most)
        BasicGeoposition at = locator.GetGeopositionAsync(minutes(60), seconds(45)).get().Coordinate().Point().Position();
        fix->lat = std::round(at.Latitude * 100) / 100;
        fix->lon = std::round(at.Longitude * 100) / 100;
        fix->ok = true;
    } catch (...) {
        // Location off, denied, or no answer: the time zone's city stands in.
    }
    winrt::uninit_apartment();
    if (!PostMessageW(request.window, request.message, 0, reinterpret_cast<LPARAM>(fix))) delete fix;
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
        PostMessageW(window, message, 0, reinterpret_cast<LPARAM>(new Fix{false, 0, 0}));
    }
}

} // namespace bb::Location
