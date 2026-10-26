#include "core/Hotkeys.h"
#include "platform/Text.h"
#include <cstdlib>
#include <map>
#include <vector>

namespace bb::Hotkeys {
namespace {

const wchar_t* const Defaults[Count] = {
    L"Ctrl+Alt+Shift+F", L"Ctrl+Alt+Shift+Up", L"Ctrl+Alt+Shift+Down", L"Ctrl+Alt+Shift+Right", L"Ctrl+Alt+Shift+Left",
    L"Ctrl+Alt+Shift+R", L"Ctrl+Alt+Shift+K", L"Ctrl+Alt+Shift+M", L"Ctrl+Alt+Shift+B", L"Ctrl+Alt+Shift+N",
    L"Ctrl+Alt+Up", L"Ctrl+Alt+Down", L"Ctrl+Alt+Left", L"Ctrl+Alt+Right", L"Ctrl+Alt+Oemplus", L"Ctrl+Alt+OemMinus",
};

std::map<int, std::wstring> Read(const Settings& s)
{
    std::map<int, std::wstring> keys;
    std::wstring_view all = s.Hotkeys;
    while (!all.empty()) {
        size_t semi = all.find(L';');
        std::wstring_view part = all.substr(0, semi);
        all = semi == std::wstring_view::npos ? std::wstring_view{} : all.substr(semi + 1);
        size_t eq = part.find(L'=');
        if (eq == std::wstring_view::npos || eq == 0) continue;
        int id = _wtoi(std::wstring(part.substr(0, eq)).c_str());
        if (id >= 1 && id <= Count) keys[id] = std::wstring(part.substr(eq + 1));
    }
    return keys;
}

} // namespace

const wchar_t* const Names[Count] = {
    L"Screen filter on or off", L"Brighter", L"Dimmer", L"Warmer (more blue light filter)", L"Cooler (less blue light filter)",
    L"Reading strip on or off", L"Keep PC awake on or off", L"Next screen mode", L"Take a break now", L"Mini mode on or off",
    L"Move the reading strip up", L"Move the reading strip down", L"Move the reading strip left", L"Move the reading strip right",
    L"Make the reading strip bigger", L"Make the reading strip smaller",
};

bool Repeats(int id) { return id == Brighter || id == Dimmer || id == Warmer || id == Cooler || id >= StripUp; }

std::wstring Get(const Settings& s, int id)
{
    auto keys = Read(s);
    auto found = keys.find(id);
    return found != keys.end() ? found->second : Defaults[id - 1];
}

void Set(Settings& s, int id, const std::wstring& keys)
{
    auto all = Read(s);
    if (keys == Defaults[id - 1]) all.erase(id);
    else all[id] = keys;
    std::wstring joined;
    for (auto& [key, value] : all) joined += (joined.empty() ? L"" : L";") + std::to_wstring(key) + L"=" + value;
    s.Hotkeys = joined;
}

std::wstring Pretty(const std::wstring& keys)
{
    if (keys.empty()) return L"";
    std::wstring t = ReplaceAll(keys, L"+Up", L"+↑");
    t = ReplaceAll(t, L"+Down", L"+↓");
    t = ReplaceAll(t, L"+Left", L"+←");
    t = ReplaceAll(t, L"+Right", L"+→");
    t = ReplaceAll(t, L"Oemplus", L"=");
    t = ReplaceAll(t, L"OemMinus", L"-");
    return ReplaceAll(t, L"+", L" + ");
}

} // namespace bb::Hotkeys
