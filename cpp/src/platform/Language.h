#pragma once
#include <string>
#include <string_view>

namespace bb {

// The translations built into the exe (res/app.rc, LANG_<CODE>), and the language Windows is shown in.
std::string LanguageText(std::wstring_view code);   // the language's lines, as UTF-8; "" if there is none
std::wstring SystemLanguage();                      // two letters ("tr" for Turkish)

} // namespace bb
