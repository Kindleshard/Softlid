#pragma once
#include "core/Palette.h"
#include <string>

namespace bb {

using text = std::wstring;

// Everything you can set, as plain fields (listed once, in SettingsFields.h). Loading also brings old settings files
// up to date and keeps every value in its range; saving writes the whole file again.
struct Settings {
#define BB_SETTING(type, name, value) type name = value;
#include "core/SettingsFields.h"
#undef BB_SETTING

    static constexpr int CustomLevel = 4;   // ComfortLevel's "custom curve" (standard engine only)

    // The preset strength the engines use (custom exists only for the standard engine).
    int Level() const { return ComfortLevel < CustomLevel ? ComfortLevel : CustomLevel - 1; }
    bool CustomTone() const { return ToneMode == 0 && ComfortLevel == CustomLevel; }

    static Settings Load();
    void Save() const;
    static text FilePath();   // %AppData%\BlinkBuddy\settings.ini

private:
    void Clamp();
};

} // namespace bb
