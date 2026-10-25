#include "features/screen/Backlight.h"
#include <highlevelmonitorconfigurationapi.h>
#include <physicalmonitorenumerationapi.h>
#include <oleauto.h>
#include <wbemidl.h>
#include <wrl/client.h>
#include <cmath>
#include <functional>
#include <mutex>
#include <vector>

#pragma comment(lib, "dxva2.lib")
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "oleaut32.lib")

namespace bb::Backlight {
namespace {

using Microsoft::WRL::ComPtr;

std::mutex gate;   // pending and busy
std::mutex bus;    // one conversation with the monitors at a time
int pending = -1;
bool busy = false;

// Calls act(monitor, min, current, max) for every physical monitor that reports a brightness range; returns how many
// physical monitors there are.
int ForEachMonitor(const std::function<void(HANDLE, DWORD, DWORD, DWORD)>& act)
{
    std::lock_guard lock(bus);
    std::vector<HMONITOR> handles;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR m, HDC, LPRECT, LPARAM list) {
        reinterpret_cast<std::vector<HMONITOR>*>(list)->push_back(m);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&handles));
    int total = 0;
    for (HMONITOR m : handles) {
        DWORD n = 0;
        if (!GetNumberOfPhysicalMonitorsFromHMONITOR(m, &n) || n == 0) continue;
        std::vector<PHYSICAL_MONITOR> monitors(n);
        if (!GetPhysicalMonitorsFromHMONITOR(m, n, monitors.data())) continue;
        total += n;
        for (const PHYSICAL_MONITOR& p : monitors) {
            DWORD min, cur, max;
            if (GetMonitorBrightness(p.hPhysicalMonitor, &min, &cur, &max)) act(p.hPhysicalMonitor, min, cur, max);
        }
        DestroyPhysicalMonitors(n, monitors.data());
    }
    return total;
}

// Built-in laptop screens don't speak DDC/CI; Windows exposes them through WMI instead. (On a worker thread, in its
// own multithreaded apartment.)
ComPtr<IWbemServices> ConnectWmi()
{
    ComPtr<IWbemLocator> locator;
    ComPtr<IWbemServices> services;
    if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&locator)))) return nullptr;
    BSTR space = SysAllocString(L"root\\WMI");
    HRESULT hr = locator->ConnectServer(space, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services);
    SysFreeString(space);
    if (FAILED(hr)) return nullptr;
    CoSetProxyBlanket(services.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                      RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    return services;
}

// Each object a WMI query returns, until act returns false.
void Query(IWbemServices* wmi, const wchar_t* wql, const std::function<bool(IWbemClassObject*)>& act)
{
    BSTR language = SysAllocString(L"WQL"), query = SysAllocString(wql);
    ComPtr<IEnumWbemClassObject> results;
    HRESULT hr = wmi->ExecQuery(language, query, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &results);
    SysFreeString(language);
    SysFreeString(query);
    if (FAILED(hr)) return;
    for (;;) {
        ComPtr<IWbemClassObject> o;
        ULONG got = 0;
        if (results->Next(5000, 1, &o, &got) != WBEM_S_NO_ERROR || got == 0 || !act(o.Get())) return;
    }
}

int LaptopPanelBrightness(IWbemServices* wmi)
{
    int level = -1;
    Query(wmi, L"SELECT CurrentBrightness FROM WmiMonitorBrightness", [&](IWbemClassObject* o) {
        VARIANT v;
        VariantInit(&v);
        if (SUCCEEDED(o->Get(L"CurrentBrightness", 0, &v, nullptr, nullptr)) && v.vt == VT_UI1) level = v.bVal;
        VariantClear(&v);
        return false;
    });
    return level;
}

void SetLaptopPanel(IWbemServices* wmi, int percent)
{
    ComPtr<IWbemClassObject> methods, inClass;
    BSTR className = SysAllocString(L"WmiMonitorBrightnessMethods"), method = SysAllocString(L"WmiSetBrightness");
    if (SUCCEEDED(wmi->GetObject(className, 0, nullptr, &methods, nullptr)) &&
        SUCCEEDED(methods->GetMethod(method, 0, &inClass, nullptr))) {
        Query(wmi, L"SELECT __PATH FROM WmiMonitorBrightnessMethods", [&](IWbemClassObject* o) {
            VARIANT path, timeout, level;
            VariantInit(&path);
            ComPtr<IWbemClassObject> in;
            if (SUCCEEDED(o->Get(L"__PATH", 0, &path, nullptr, nullptr)) && path.vt == VT_BSTR && SUCCEEDED(inClass->SpawnInstance(0, &in))) {
                timeout.vt = VT_I4;   // (WMI takes a uint32 as VT_I4)
                timeout.lVal = 1;
                level.vt = VT_UI1;
                level.bVal = static_cast<BYTE>(percent);
                in->Put(L"Timeout", 0, &timeout, 0);
                in->Put(L"Brightness", 0, &level, 0);
                wmi->ExecMethod(path.bstrVal, method, 0, nullptr, in.Get(), nullptr, nullptr);
            }
            VariantClear(&path);
            return true;
        });
    }
    SysFreeString(className);
    SysFreeString(method);
}

// Sets the latest value asked for, again and again until no newer one is waiting.
DWORD WINAPI Drain(void*)
{
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IWbemServices> wmi = ConnectWmi();
    for (;;) {
        int p;
        {
            std::lock_guard lock(gate);
            p = pending;
            pending = -1;
            if (p < 0) {
                busy = false;
                break;
            }
        }
        ForEachMonitor([p](HANDLE h, DWORD min, DWORD, DWORD max) {
            SetMonitorBrightness(h, min + static_cast<DWORD>(std::lround((max - min) * p / 100.0)));
        });
        if (wmi) SetLaptopPanel(wmi.Get(), p);
    }
    wmi.Reset();
    if (SUCCEEDED(com)) CoUninitialize();
    return 0;
}

struct ProbeRequest { HWND window; UINT message; };

DWORD WINAPI ProbeWork(void* param)
{
    ProbeRequest request = *static_cast<ProbeRequest*>(param);
    delete static_cast<ProbeRequest*>(param);
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    int supported = 0, first = -1;
    int total = ForEachMonitor([&](HANDLE, DWORD min, DWORD cur, DWORD max) {
        supported++;
        if (first < 0) first = max > min ? static_cast<int>(std::lround(100.0 * (cur - min) / (max - min))) : 100;
    });
    if (ComPtr<IWbemServices> wmi = ConnectWmi()) {
        int panel = LaptopPanelBrightness(wmi.Get());
        if (panel >= 0) {
            supported++;
            if (first < 0) first = panel;
        }
    }
    if (SUCCEEDED(com)) CoUninitialize();
    PostMessageW(request.window, request.message, MAKEWPARAM(supported, total > supported ? total : supported), first);
    return 0;
}

} // namespace

void Set(int percent)
{
    {
        std::lock_guard lock(gate);
        pending = percent;
        if (busy) return;
        busy = true;
    }
    HANDLE thread = CreateThread(nullptr, 0, Drain, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
    else {
        std::lock_guard lock(gate);
        busy = false;
    }
}

void Probe(HWND window, UINT message)
{
    auto* request = new ProbeRequest{window, message};
    HANDLE thread = CreateThread(nullptr, 0, ProbeWork, request, 0, nullptr);
    if (thread) CloseHandle(thread);
    else delete request;
}

} // namespace bb::Backlight
