#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace bb {

// Text helpers. Inside the app all text is UTF-16 (std::wstring), as Windows wants it; files are UTF-8.
std::wstring FromUtf8(std::string_view bytes);
std::string ToUtf8(std::wstring_view text);

std::wstring Trim(std::wstring_view s);
std::wstring TrimEnd(std::wstring_view s);
std::vector<std::wstring> SplitLines(std::wstring_view s);   // on \n, dropping \r
bool EqualsNoCase(std::wstring_view a, std::wstring_view b);
std::wstring ReplaceAll(std::wstring s, std::wstring_view from, std::wstring_view to);

} // namespace bb
