#include "core/Modes.h"
#include "platform/Text.h"
#include <algorithm>
#include <cwchar>

namespace bb::Modes {
namespace {

struct Preset { int brightness, blue, effect; };

const Preset Presets[Custom] = {
    {100, 0, 0},    // Pause (the filter is switched off)
    {75, 65, 0},    // Health: dimmer and warm, for long sessions
    {100, 10, 0},   // Game: bright, a touch warm
    {90, 25, 0},    // Movie
    {90, 35, 0},    // Office
    {100, 0, 0},    // Editing: true colour (and no tone curve)
    {85, 50, 0},    // Reading: warm, a little dimmer
};

} // namespace

const wchar_t* const Names[Count] = {L"Pause", L"Health", L"Game", L"Movie", L"Office", L"Editing", L"Reading", L"Custom"};
// pause, heart, game pad, film, briefcase, pen, book, sliders
const wchar_t* const Glyphs[Count] = {L"\uE769", L"\uEB51", L"\uE7FC", L"\uE8B2", L"\uE821", L"\uE70F", L"\uE736", L"\uE9E9"};

void Apply(Settings& s, int mode)
{
    if (mode == Custom) {
        s.FilterEnabled = s.KeptFilter;
        s.Brightness = s.KeptBrightness;
        s.BlueFilter = s.CoolRange ? s.KeptBlue : std::max(0, s.KeptBlue);   // (the cool side may have been switched off since)
        s.ColorEffect = s.KeptEffect;
        s.ToneEnabled = s.KeptTone;
        return;
    }
    if (mode < 0 || mode >= Custom) return;
    s.FilterEnabled = mode != Pause;
    if (mode == Pause) return;
    s.Brightness = Presets[mode].brightness;
    s.BlueFilter = Presets[mode].blue;
    s.ColorEffect = Presets[mode].effect;
    // Editing: true colour; the others keep your own tone setting (the live one while your own values show, as for a
    // rule applied over Custom; otherwise the one kept aside).
    bool ownTone = s.Mode == Custom || s.Mode < 0 ? s.ToneEnabled : s.KeptTone;
    s.ToneEnabled = mode != Editing && ownTone;
}

void Choose(Settings& s, int mode)
{
    if (s.Mode == Custom || s.Mode < 0) KeepCustom(s);
    s.Mode = mode;
    Apply(s, mode);
}

void KeepCustom(Settings& s)
{
    s.KeptFilter = s.FilterEnabled;
    s.KeptBrightness = s.Brightness;
    s.KeptBlue = s.BlueFilter;
    s.KeptEffect = s.ColorEffect;
    s.KeptTone = s.ToneEnabled;
}

std::vector<Rule> Rules(const Settings& s)
{
    std::vector<Rule> list;
    std::wstring_view all = s.AppRules;
    while (!all.empty()) {
        size_t bar = all.find(L'|');
        std::wstring_view part = all.substr(0, bar);
        all = bar == std::wstring_view::npos ? std::wstring_view{} : all.substr(bar + 1);
        // app=mode, or app=mode=path (a path may hold more '=': only the first two split)
        size_t first = part.find(L'=');
        if (first == std::wstring_view::npos) continue;
        size_t second = part.find(L'=', first + 1);
        std::wstring app = Lower(Trim(part.substr(0, first)));
        std::wstring number(part.substr(first + 1, second == std::wstring_view::npos ? std::wstring_view::npos : second - first - 1));
        wchar_t* end = nullptr;
        long mode = std::wcstol(number.c_str(), &end, 10);
        if (app.empty() || number.empty() || *end != L'\0') continue;
        list.push_back({app, static_cast<int>(mode), second == std::wstring_view::npos ? L"" : std::wstring(part.substr(second + 1))});
    }
    return list;
}

void SaveRules(Settings& s, const std::vector<Rule>& rules)
{
    std::wstring all;
    for (const Rule& r : rules) {
        if (!all.empty()) all += L'|';
        all += r.app + L'=' + std::to_wstring(r.mode);
        if (!r.path.empty()) all += L'=' + r.path;
    }
    s.AppRules = all;
}

int RuleFor(const Settings& s, const std::wstring& app)
{
    if (app.empty()) return -1;
    for (const Rule& r : Rules(s)) if (r.app == app) return r.mode;
    return -1;
}

} // namespace bb::Modes
