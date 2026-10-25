#pragma once
#include <windows.h>
#include <string>

namespace bb::ProgramInfo {

// A program's own name ("Google Chrome") from its file, or its file name without ".exe" ("Code").
std::wstring Name(const std::wstring& app, const std::wstring& path);

// A program's icon at a size in pixels (the caller destroys it), or null.
HICON Icon(const std::wstring& path, int size);

} // namespace bb::ProgramInfo
