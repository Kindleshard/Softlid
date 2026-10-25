#include "core/Lang.h"
#include "platform/Text.h"
#include <windows.h>
#include <unordered_map>
#include <vector>

namespace bb::Lang {
namespace {

constexpr Info Languages[] = {
    {L"en", L"English"}, {L"tr", L"Türkçe"}, {L"de", L"Deutsch"}, {L"fr", L"Français"},
    {L"es", L"Español"}, {L"pt", L"Português (Brasil)"}, {L"it", L"Italiano"}, {L"nl", L"Nederlands"},
    {L"pl", L"Polski"}, {L"cs", L"Čeština"}, {L"hu", L"Magyar"}, {L"ro", L"Română"},
    {L"sv", L"Svenska"}, {L"da", L"Dansk"}, {L"fi", L"Suomi"}, {L"el", L"Ελληνικά"},
    {L"ru", L"Русский"}, {L"uk", L"Українська"}, {L"ja", L"日本語"}, {L"ko", L"한국어"},
    {L"zh", L"简体中文"},
};

std::wstring current = L"en";
std::unordered_map<std::wstring, std::wstring> table;   // English line -> translated line (empty for English)

bool Known(std::wstring_view code)
{
    for (const Info& i : Languages) if (code == i.code) return true;
    return false;
}

// A language's lines, from the exe's resources (LANG_<CODE>).
std::vector<std::wstring> Read(std::wstring_view code)
{
    std::wstring name = L"LANG_";
    for (wchar_t c : code) name += static_cast<wchar_t>(towupper(c));
    HRSRC res = FindResourceW(nullptr, name.c_str(), RT_RCDATA);
    if (!res) return {};
    HGLOBAL data = LoadResource(nullptr, res);
    auto bytes = static_cast<const char*>(LockResource(data));
    if (!bytes) return {};
    return SplitLines(FromUtf8(std::string_view(bytes, SizeofResource(nullptr, res))));
}

std::wstring Unescape(const std::wstring& s) { return TrimEnd(ReplaceAll(s, L"\\n", L"\n")); }

// The Windows display language as a two-letter code ("tr" for Turkish).
std::wstring WindowsLanguage()
{
    wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
    LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), locale, LOCALE_NAME_MAX_LENGTH, 0);
    std::wstring code(locale);
    return code.substr(0, code.find(L'-'));
}

} // namespace

std::span<const Info> All() { return Languages; }

const std::wstring& Code() { return current; }

void Use(std::wstring_view code)
{
    std::wstring wanted = code.empty() ? WindowsLanguage() : std::wstring(code);
    current = Known(wanted) ? wanted : L"en";
    table.clear();
    if (current == L"en") return;
    std::vector<std::wstring> en = Read(L"en"), here = Read(current);
    table.reserve(en.size());
    for (size_t i = 0; i < en.size() && i < here.size(); i++)
        if (!en[i].empty() && !here[i].empty()) table[Unescape(en[i])] = Unescape(here[i]);
}

std::wstring T(std::wstring_view english)
{
    if (table.empty()) return std::wstring(english);
    auto found = table.find(std::wstring(english));
    return found != table.end() ? found->second : std::wstring(english);
}

std::wstring F(std::wstring_view english, std::initializer_list<std::wstring> args)
{
    std::wstring pattern = T(english), out;
    out.reserve(pattern.size() + 16);
    for (size_t i = 0; i < pattern.size(); i++) {
        size_t close = pattern.find(L'}', i);
        if (pattern[i] == L'{' && close != std::wstring::npos && i + 1 < close && iswdigit(pattern[i + 1])) {
            size_t index = static_cast<size_t>(_wtoi(pattern.c_str() + i + 1));   // stops at ':' or '}'
            if (index < args.size()) out += *(args.begin() + index);
            i = close;
            continue;
        }
        out += pattern[i];
    }
    return out;
}

const wchar_t* Family()
{
    if (current == L"ja") return L"Yu Gothic UI";
    if (current == L"ko") return L"Malgun Gothic";
    if (current == L"zh") return L"Microsoft YaHei UI";
    return L"Segoe UI";
}

} // namespace bb::Lang
