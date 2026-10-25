#pragma once
#include "core/Modes.h"
#include <windows.h>
#include <functional>
#include <string>
#include <vector>

namespace bb::AppRuleDialog {

// Adding a mode for an app, in one place: pick the app (one that's open now, each with its icon and name, or a
// program file found with Browse), pick its mode, Add. Apps in `taken` already have a rule and aren't offered. The
// owner is disabled while it shows; added gets the new rule (not called if cancelled).
void Open(HWND owner, const std::vector<std::wstring>& taken, std::function<void(const Modes::Rule&)> added,
          bool activate = true);   // activate false: diagnostics (no focus taken)

} // namespace bb::AppRuleDialog
