#include "core/Settings.h"
#include "platform/Files.h"
#include "platform/Text.h"
#include <algorithm>
#include <cwchar>
#include <set>

#define BB_WIDEN2(x) L##x
#define BB_WIDEN(x) BB_WIDEN2(x)

namespace bb {
namespace {

// Values as the file holds them (bools as True/False, like the C# version wrote them).
void Parse(const text& v, bool& out)
{
    if (EqualsNoCase(v, L"True")) out = true;
    else if (EqualsNoCase(v, L"False")) out = false;
}

void Parse(const text& v, int& out)
{
    if (v.empty()) return;
    wchar_t* end = nullptr;
    long long n = std::wcstoll(v.c_str(), &end, 10);
    if (end && *end == L'\0' && n >= INT_MIN && n <= INT_MAX) out = static_cast<int>(n);
}

void Parse(const text& v, text& out) { out = v; }

text Show(bool b) { return b ? L"True" : L"False"; }
text Show(int n) { return std::to_wstring(n); }
const text& Show(const text& s) { return s; }

int Fit(int v, int lo, int hi) { return std::clamp(v, lo, hi); }

} // namespace

namespace {
text fileOverride;   // (diagnostics: --settings)
}

void Settings::UseFile(const text& path) { fileOverride = path; }

text Settings::FilePath() { return fileOverride.empty() ? RoamingAppData() + L"\\BlinkBuddy\\settings.ini" : fileOverride; }

Settings Settings::Load()
{
    std::optional<std::string> bytes = ReadAllBytes(FilePath());
    return bytes ? FromText(FromUtf8(*bytes)) : Settings{};   // first start: the defaults
}

Settings Settings::FromText(std::wstring_view file)
{
    Settings s;
    std::set<text> seen;
    for (const text& line : SplitLines(file)) {
        size_t eq = line.find(L'=');
        if (eq == text::npos || eq == 0) continue;
        text key = Trim(std::wstring_view(line).substr(0, eq)), value = Trim(std::wstring_view(line).substr(eq + 1));
        bool known = false;
#define BB_SETTING(type, name, value_) if (!known && key == BB_WIDEN(#name)) { Parse(value, s.name); known = true; }
#include "core/SettingsFields.h"
#undef BB_SETTING
        if (known) seen.insert(key);
    }

    // Before the schedule had a strength of its own it brought the blue light filter in only at night; keep it that
    // way: the old value becomes the night one, and days stay neutral.
    if (!seen.count(L"NightBlue") && s.ScheduleEnabled) {
        s.NightBlue = s.BlueFilter;
        s.BlueFilter = 0;
    }
    // Before there was an edge choice, the soft edge and the outline could both be on (0 meaning off).
    if (!seen.count(L"StripEdge")) s.StripEdge = s.StripBorder > 0 ? 2 : s.StripFeather > 0 ? 1 : 0;

    s.Clamp();
    return s;
}

void Settings::Save() const { WriteAllBytesSafely(FilePath(), ToUtf8(ToText())); }

text Settings::ToText() const
{
    text out;
#define BB_SETTING(type, name, value_) out += BB_WIDEN(#name); out += L'='; out += Show(name); out += L"\r\n";
#include "core/SettingsFields.h"
#undef BB_SETTING
    return out;
}

// Every value back in its range, and old forms of some brought up to date.
void Settings::Clamp()
{
    Brightness = Fit(Brightness, 20, 100);
    BlueFilter = Fit(BlueFilter, CoolRange ? -75 : 0, 100);
    ColorEffect = Fit(ColorEffect, 0, 4);
    GrayStyle = Fit(GrayStyle, 0, 2);
    Mode = Fit(Mode, -1, 7);
    KeptBrightness = Fit(KeptBrightness, 20, 100);
    KeptBlue = Fit(KeptBlue, -75, 100);
    KeptEffect = Fit(KeptEffect, 0, 4);
    FocusWork = Fit(FocusWork, 5, 90);
    FocusRest = Fit(FocusRest, 1, 30);
    BlinkInterval = Fit(BlinkInterval, 3, 120);
    IconSize = Fit(IconSize, 32, 256);
    IconOpacity = Fit(IconOpacity, 10, 100);
    IconCorner = Fit(IconCorner, 0, 4);
    BreakInterval = Fit(BreakInterval, 5, 60);
    BreakDuration = Fit(BreakDuration, 10, 120);
    HardwareRestore = Fit(HardwareRestore, -1, 100);
    if (HardwareBrightness && Backlight < 0) {
        // Before the backlight had its own slider, Brightness drove it in this mode.
        Backlight = Brightness;
        Brightness = 100;
    }
    Backlight = Fit(Backlight, -1, 100);
    NightFade = Fit(NightFade, 0, 180);
    BlinkFadeDepth = Fit(BlinkFadeDepth, 10, 100);
    BlinkStyle = Fit(BlinkStyle, 0, 1);
    ToneMode = Fit(ToneMode, 0, 4);
    ComfortLevel = Fit(ComfortLevel, 0, CustomLevel);
    ShadowCrush = Fit(ShadowCrush, 0, 100);
    Squeeze = Fit(Squeeze, 0, 100);
    LimiterStrength = Fit(LimiterStrength, 0, 100);
    if (ToneMode == 1) {
        // The old manual mode is the standard engine with custom strength; all sliders at zero meant no tone.
        ToneMode = 0;
        ComfortLevel = CustomLevel;
        if (ShadowCrush == 0 && Squeeze == 0 && LimiterStrength == 0) ToneEnabled = false;
    }
    if (ToneMode != 0 && ComfortLevel == CustomLevel) ComfortLevel = 1;
    ScheduleMode = Fit(ScheduleMode, 0, 1);
    NightFrom = Fit(NightFrom, 0, 1439);
    NightTo = Fit(NightTo, 0, 1439);
    StripHeight = Fit(StripHeight, 40, 400);
    StripDim = Fit(StripDim, 30, 95);
    GrainStrength = Fit(GrainStrength, 5, 100);
    GrainTone = Fit(GrainTone, 0, 2);
    StripEdge = Fit(StripEdge, 0, 2);
    StripFeather = Fit(StripFeather, 4, 200);
    StripShape = StripShape == 4 ? 0 : Fit(StripShape, 0, 3);   // 4 was "fill"
    StripWidth = Fit(StripWidth, 80, 2000);
    StripCover = Fit(StripCover, 0, 2);
    if (StripColor >= 0 && StripColor < static_cast<int>(std::size(Palette::StripTints))) StripColor = Palette::StripTints[StripColor];
    StripBorder = Fit(StripBorder, 1, 12);
    StripAccel = Fit(StripAccel, 0, 100);
    LongBreakInterval = Fit(LongBreakInterval, 30, 120);
    LongBreakDuration = Fit(LongBreakDuration, 1, 15);
    MiniFadeDelay = Fit(MiniFadeDelay, 10, 1000);
    MiniFadeTime = Fit(MiniFadeTime, 0, 200);
    MiniOpacity = Fit(MiniOpacity, 10, 100);
    NightBlue = Fit(NightBlue, 0, 100);
    MiniIcon = Fit(MiniIcon, 0, 3);
    MiniOrbitSeconds = Fit(MiniOrbitSeconds, 0, 200);
    if (AccentColor >= 0 && AccentColor < static_cast<int>(std::size(Palette::Accents))) AccentColor = Palette::Accents[AccentColor];
    if (MiniOrbitColor > 0 && MiniOrbitColor < static_cast<int>(std::size(Palette::Glows))) MiniOrbitColor = Palette::Glows[MiniOrbitColor];
    CloseAction = Fit(CloseAction, 0, 2);
    if (SettingsHeight != 0) SettingsHeight = Fit(SettingsHeight, 360, 2000);
    SettingsZoom = Fit((SettingsZoom + 5) / 10 * 10, 80, 120);   // (to the nearest step offered: 80, 90, 100, 110 or 120)
}

} // namespace bb
