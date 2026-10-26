#pragma once
#include "core/Palette.h"
#include <string>
#include <string_view>

namespace bb {

using text = std::wstring;

// Everything you can set, as plain fields (listed once, in SettingsFields.h). Loading also brings old settings files
// up to date and keeps every value in its range; saving writes the whole file again.
struct Settings {
#define BB_SETTING(type, name, value) type name = value;
#include "core/SettingsFields.h"
#undef BB_SETTING

    static constexpr int CustomLevel = 4;   // ComfortLevel's "custom curve" (standard engine only)
    static constexpr int ToneStandard = 0, ToneSmartGpu = 2, ToneSmartLayer = 3, ToneDesktopLut = 4;   // ToneMode's engines

    // The preset strength the engines use (custom exists only for the standard engine).
    int Level() const { return ComfortLevel < CustomLevel ? ComfortLevel : CustomLevel - 1; }
    bool CustomTone() const { return ToneMode == 0 && ComfortLevel == CustomLevel; }

    static Settings Load();
    void Save() const;
    // The file's contents ("Name=Value" lines), without the file: what Load and Save read and write.
    static Settings FromText(std::wstring_view file);
    text ToText() const;
    static text FilePath();   // %AppData%\BlinkBuddy\settings.ini
    static void UseFile(const text& path);   // diagnostics: another file instead (--settings)

private:
    void Clamp();
};

} // namespace bb
