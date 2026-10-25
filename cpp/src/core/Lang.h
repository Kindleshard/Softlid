#pragma once
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>

namespace bb::Lang {

// The interface language. Text is written in English in the code and looked up as it reaches the screen:
// res/lang/en.txt lists every translatable line, and res/lang/<code>.txt holds the same lines, in the same order,
// in that language (an empty or missing line stays English). "\n" in a line stands for a line break.

struct Info { const wchar_t* code; const wchar_t* name; };   // the name in its own words
std::span<const Info> All();

void Use(std::wstring_view code);   // "" = the Windows display language, if there is a translation for it
const std::wstring& Code();

std::wstring T(std::wstring_view english);

// A sentence with blanks: the English pattern ("Next break in {0} min") is looked up, then {0}, {1}... are filled in
// with the given text (a format after a colon, as in {0:0}, is ignored: pass the value already formatted).
std::wstring F(std::wstring_view english, std::initializer_list<std::wstring> args);

// The font family for text this language needs (CJK languages have their own UI fonts).
const wchar_t* Family();

} // namespace bb::Lang
