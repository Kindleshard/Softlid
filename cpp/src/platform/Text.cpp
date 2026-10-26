#include "platform/Text.h"
#include <windows.h>
#include <algorithm>

namespace bb {

std::wstring FromUtf8(std::string_view bytes)
{
    if (bytes.size() >= 3 && bytes.substr(0, 3) == "\xEF\xBB\xBF") bytes.remove_prefix(3);   // a byte-order mark
    if (bytes.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), out.data(), n);
    return out;
}

std::string ToUtf8(std::wstring_view text)
{
    if (text.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n, nullptr, nullptr);
    return out;
}

static bool IsSpace(wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n'; }

std::wstring Trim(std::wstring_view s)
{
    while (!s.empty() && IsSpace(s.front())) s.remove_prefix(1);
    while (!s.empty() && IsSpace(s.back())) s.remove_suffix(1);
    return std::wstring(s);
}

std::wstring TrimEnd(std::wstring_view s)
{
    while (!s.empty() && IsSpace(s.back())) s.remove_suffix(1);
    return std::wstring(s);
}

std::vector<std::wstring> SplitLines(std::wstring_view s)
{
    std::vector<std::wstring> lines;
    size_t start = 0;
    while (start <= s.size()) {
        size_t end = s.find(L'\n', start);
        if (end == std::wstring_view::npos) end = s.size();
        std::wstring_view line = s.substr(start, end - start);
        if (!line.empty() && line.back() == L'\r') line.remove_suffix(1);
        lines.emplace_back(line);
        start = end + 1;
    }
    return lines;
}

bool EqualsNoCase(std::wstring_view a, std::wstring_view b)
{
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

std::wstring Lower(std::wstring s)
{
    if (!s.empty())
        LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, s.c_str(), static_cast<int>(s.size()), s.data(),
                      static_cast<int>(s.size()), nullptr, nullptr, 0);
    return s;
}

std::wstring ReplaceAll(std::wstring s, std::wstring_view from, std::wstring_view to)
{
    for (size_t at = s.find(from); at != std::wstring::npos; at = s.find(from, at + to.size()))
        s.replace(at, from.size(), to);
    return s;
}

std::wstring FoldForSearch(const std::wstring& s)
{
    std::wstring t = Trim(s);
    for (wchar_t& c : t) if (c == L'İ' || c == L'ı') c = L'i';   // Turkish dotted / dotless i
    if (t.empty()) return t;
    int n = NormalizeString(NormalizationD, t.c_str(), static_cast<int>(t.size()), nullptr, 0);
    std::wstring d(n > 0 ? n : 0, L'\0');
    if (n > 0) d.resize(std::max(0, NormalizeString(NormalizationD, t.c_str(), static_cast<int>(t.size()), d.data(), n)));
    else d = t;
    std::vector<WORD> types(d.size());
    GetStringTypeW(CT_CTYPE3, d.c_str(), static_cast<int>(d.size()), types.data());
    std::wstring out;
    for (size_t i = 0; i < d.size(); i++) if (!(types[i] & C3_NONSPACING)) out += d[i];
    CharLowerBuffW(out.data(), static_cast<DWORD>(out.size()));
    return out;
}

} // namespace bb
