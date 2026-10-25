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

std::wstring RoamingAppData();   // %AppData%
std::wstring ExeFolder();        // the folder BlinkBuddy.exe is in

} // namespace bb
