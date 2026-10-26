#pragma once
#include "core/Settings.h"
#include <string>

namespace bb {

// What the tone lab needs from the app (App implements it): the settings it changes freely while open, the engine
// to switch to, the hold-to-compare, and the guard's and DesktopLUT's status lines.
class ToneLabHost {
public:
    virtual Settings& S() = 0;
    virtual void Changed(bool screen = false) = 0;
    virtual void SetToneMode(int mode) = 0;
    virtual void LabCompare(bool without) = 0;   // the screen without tone while held
    virtual const std::wstring& GuardNote() const = 0;
    virtual const std::wstring& LutStatus() const = 0;

protected:
    ~ToneLabHost() = default;
};

} // namespace bb
