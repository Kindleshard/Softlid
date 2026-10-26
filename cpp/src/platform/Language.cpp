#include "platform/Language.h"
#include <windows.h>
#include <cwctype>

namespace bb {

std::string LanguageText(std::wstring_view code)
{
    std::wstring name = L"LANG_";
    for (wchar_t c : code) name += static_cast<wchar_t>(towupper(c));
    HRSRC res = FindResourceW(nullptr, name.c_str(), RT_RCDATA);
    if (!res) return {};
    HGLOBAL data = LoadResource(nullptr, res);
    auto bytes = static_cast<const char*>(LockResource(data));
    return bytes ? std::string(bytes, SizeofResource(nullptr, res)) : std::string{};
}

std::wstring SystemLanguage()
{
    wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
    LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), locale, LOCALE_NAME_MAX_LENGTH, 0);
    std::wstring code(locale);
    return code.substr(0, code.find(L'-'));
}

} // namespace bb
