#pragma once
#include "BuildVersion.h"

namespace bb {

#define BB_WIDEN2(x) L##x
#define BB_WIDEN(x) BB_WIDEN2(x)

// This version of BlinkBuddy (the updater compares it with the latest release; set in res/BuildVersion.h).
inline constexpr wchar_t Version[] = BB_WIDEN(BB_VERSION_TEXT);
inline constexpr int VersionParts[3] = {BB_VERSION_MAJOR, BB_VERSION_MINOR, BB_VERSION_PATCH};

// Where releases are published, as "owner/name" on GitHub, and where bugs are reported. Empty: no release channel yet
// (the updater says so, and the bug report row stays hidden).
inline constexpr wchar_t ReleaseRepo[] = L"";

// A page for supporting BlinkBuddy. Empty: the Support row stays hidden.
inline constexpr wchar_t DonateUrl[] = L"https://ko-fi.com/P2Z229LS9G";

} // namespace bb
