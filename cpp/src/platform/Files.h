#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace bb {

// Reading and writing whole files, and where they live.
std::optional<std::string> ReadAllBytes(const std::wstring& path);

// Writes to a temporary file next to it, then swaps it in: a crash or power cut mid-write never leaves half a file.
// The folder is created if needed.
bool WriteAllBytesSafely(const std::wstring& path, std::string_view bytes);

// Text files, as UTF-8 on disk: "" if it can't be read; written safely, as above.
std::wstring ReadAllText(const std::wstring& path);
bool WriteAllText(const std::wstring& path, std::wstring_view text);

std::wstring RoamingAppData();   // %AppData%
std::wstring ExePath();          // Softlid.exe itself
std::wstring ExeFolder();        // the folder it is in
std::wstring TempFolder();       // %Temp%, ending in a backslash

} // namespace bb
