#pragma once
#include "core/Settings.h"
#include <string>
#include <vector>

namespace bb::Modes {

// One-click screen modes, and modes chosen per app.
// A mode is a set of screen settings: Pause turns the filter off; Health, Game, Movie, Office, Editing and Reading set
// brightness, the blue light filter and the colour effect; Custom is your own values, kept aside while another mode
// is in use and brought back when you pick Custom again (or move a slider yourself).
// A rule ("code.exe" -> Editing) applies its mode while that app is in front, to a copy of the settings, so your own
// values are untouched and come back when you switch away. The night schedule leaves it as it is.
enum Mode { Pause = 0, Health, Game, Movie, Office, Editing, Reading, Custom, Count };

extern const wchar_t* const Names[Count];    // English (translated where shown)
extern const wchar_t* const Glyphs[Count];   // Segoe Fluent Icons / MDL2

void Apply(Settings& s, int mode);    // puts a mode's values into a settings object (Custom: the values kept aside)
void Choose(Settings& s, int mode);   // picking a mode by hand: your own values are put aside first, if showing
void KeepCustom(Settings& s);

// Rules, kept as "photoshop.exe=0=C:\...\Photoshop.exe|code.exe=5" (the path, when known, gives the name and icon).
struct Rule { std::wstring app; int mode; std::wstring path; };
std::vector<Rule> Rules(const Settings& s);
void SaveRules(Settings& s, const std::vector<Rule>& rules);
int RuleFor(const Settings& s, const std::wstring& app);   // -1: no rule for it

} // namespace bb::Modes
