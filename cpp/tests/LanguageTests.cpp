// The translations, packed into the exe by LangPack at build time, unpack to exactly the text in res\lang.
#include "Check.h"
#include "platform/Language.h"
#include <filesystem>
#include <fstream>
#include <iterator>

TEST(TranslationsUnpackToTheirText)
{
    int languages = 0;
    for (const auto& entry : std::filesystem::directory_iterator(BB_LANG_DIR)) {
        if (entry.path().extension() != L".txt") continue;
        std::ifstream in(entry.path(), std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(!text.empty());
        CHECK(bb::LanguageText(entry.path().stem().wstring()) == text);
        languages++;
    }
    CHECK_EQ(languages, 21);
    CHECK(bb::LanguageText(L"xx").empty());   // (no such language)
}
