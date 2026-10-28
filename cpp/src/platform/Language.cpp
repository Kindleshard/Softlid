#include "platform/Language.h"
#include <windows.h>
#include <compressapi.h>
#include <cwctype>

namespace bb {

std::string LanguageText(std::wstring_view code)
{
    std::wstring name = L"LANG_";
    for (wchar_t c : code) name += static_cast<wchar_t>(towupper(c));
    HRSRC res = FindResourceW(nullptr, name.c_str(), RT_RCDATA);
    if (!res) return {};
    auto packed = static_cast<const char*>(LockResource(LoadResource(nullptr, res)));
    DWORD size = SizeofResource(nullptr, res);
    // Packed by LangPack at build time (MSZIP): its header gives the size it unpacks to.
    DECOMPRESSOR_HANDLE unpacker = nullptr;
    if (!packed || !CreateDecompressor(COMPRESS_ALGORITHM_MSZIP, nullptr, &unpacker)) return {};
    SIZE_T length = 0;
    Decompress(unpacker, packed, size, nullptr, 0, &length);
    std::string text(length, '\0');
    if (!Decompress(unpacker, packed, size, text.data(), length, &length)) text.clear();
    CloseDecompressor(unpacker);
    return text;
}

std::wstring SystemLanguage()
{
    wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
    LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), locale, LOCALE_NAME_MAX_LENGTH, 0);
    std::wstring code(locale);
    return code.substr(0, code.find(L'-'));
}

} // namespace bb
