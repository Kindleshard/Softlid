#include "core/Hotkeys.h"
#include "platform/Text.h"
#include <windows.h>
#include <map>
#include <vector>

namespace bb::Hotkeys {
namespace {

const wchar_t* const Defaults[Count] = {
    L"Ctrl+Alt+Shift+F", L"Ctrl+Alt+Shift+Up", L"Ctrl+Alt+Shift+Down", L"Ctrl+Alt+Shift+Right", L"Ctrl+Alt+Shift+Left",
    L"Ctrl+Alt+Shift+R", L"Ctrl+Alt+Shift+K", L"Ctrl+Alt+Shift+M", L"Ctrl+Alt+Shift+B", L"Ctrl+Alt+Shift+N",
    L"Ctrl+Alt+Up", L"Ctrl+Alt+Down", L"Ctrl+Alt+Left", L"Ctrl+Alt+Right", L"Ctrl+Alt+Oemplus", L"Ctrl+Alt+OemMinus",
};

// Keys with names of their own (as Windows Forms writes them); letters, digits ("D5"), F-keys and the number pad
// ("NumPad5") are worked out.
struct Named { UINT vk; const wchar_t* name; };
constexpr Named NamedKeys[] = {
    {VK_BACK, L"Back"}, {VK_TAB, L"Tab"}, {VK_RETURN, L"Return"}, {VK_PAUSE, L"Pause"}, {VK_ESCAPE, L"Escape"},
    {VK_SPACE, L"Space"}, {VK_PRIOR, L"PageUp"}, {VK_NEXT, L"Next"}, {VK_END, L"End"}, {VK_HOME, L"Home"},
    {VK_LEFT, L"Left"}, {VK_UP, L"Up"}, {VK_RIGHT, L"Right"}, {VK_DOWN, L"Down"}, {VK_SNAPSHOT, L"PrintScreen"},
    {VK_INSERT, L"Insert"}, {VK_DELETE, L"Delete"}, {VK_MULTIPLY, L"Multiply"}, {VK_ADD, L"Add"},
    {VK_SUBTRACT, L"Subtract"}, {VK_DECIMAL, L"Decimal"}, {VK_DIVIDE, L"Divide"}, {VK_OEM_1, L"OemSemicolon"},
    {VK_OEM_PLUS, L"Oemplus"}, {VK_OEM_COMMA, L"Oemcomma"}, {VK_OEM_MINUS, L"OemMinus"}, {VK_OEM_PERIOD, L"OemPeriod"},
    {VK_OEM_2, L"OemQuestion"}, {VK_OEM_3, L"Oemtilde"}, {VK_OEM_4, L"OemOpenBrackets"}, {VK_OEM_5, L"OemPipe"},
    {VK_OEM_6, L"OemCloseBrackets"}, {VK_OEM_7, L"OemQuotes"}, {VK_OEM_102, L"OemBackslash"},
};

std::wstring KeyName(UINT vk)
{
    if ((vk >= 'A' && vk <= 'Z')) return std::wstring(1, static_cast<wchar_t>(vk));
    if (vk >= '0' && vk <= '9') return L"D" + std::wstring(1, static_cast<wchar_t>(vk));
    if (vk >= VK_F1 && vk <= VK_F24) return L"F" + std::to_wstring(vk - VK_F1 + 1);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return L"NumPad" + std::to_wstring(vk - VK_NUMPAD0);
    for (const Named& k : NamedKeys) if (k.vk == vk) return k.name;
    return L"";
}

UINT KeyCode(const std::wstring& name)
{
    for (UINT vk = 1; vk < 256; vk++) if (KeyName(vk) == name) return vk;
    if (name == L"Enter") return VK_RETURN;   // (other names Windows Forms accepts for the same keys)
    if (name == L"PageDown") return VK_NEXT;
    return 0;
}

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

bool Parse(const std::wstring& text, UINT& mods, UINT& vk)
{
    mods = vk = 0;
    std::wstring_view all = text;
    while (!all.empty()) {
        size_t plus = all.find(L'+');
        std::wstring part = Trim(all.substr(0, plus));
        all = plus == std::wstring_view::npos ? std::wstring_view{} : all.substr(plus + 1);
        if (part == L"Ctrl") mods |= MOD_CONTROL;
        else if (part == L"Alt") mods |= MOD_ALT;
        else if (part == L"Shift") mods |= MOD_SHIFT;
        else if (part == L"Win") mods |= MOD_WIN;
        else if (!(vk = KeyCode(part))) return false;
    }
    return vk != 0 && mods != 0;
}

std::wstring Describe(UINT vk, bool ctrl, bool alt, bool shift)
{
    if (vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN || vk == 0) return L"";
    std::wstring name = KeyName(vk), mods;
    if (ctrl) mods += L"Ctrl+";
    if (alt) mods += L"Alt+";
    if (shift) mods += L"Shift+";
    return mods.empty() || name.empty() ? L"" : mods + name;
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
