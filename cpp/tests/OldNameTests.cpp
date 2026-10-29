// A BlinkBuddy folder moves to Softlid's name once: the files come along, and a Softlid folder already there is left
// as it is.
#include "Check.h"
#include "app/OldName.h"
#include "platform/Files.h"
#include <filesystem>

TEST(OldFolderMovesToTheNewName)
{
    namespace fs = std::filesystem;
    fs::path root = fs::temp_directory_path() / L"softlid-oldname-test";
    fs::remove_all(root);
    fs::path from = root / L"BlinkBuddy", to = root / L"Softlid";
    fs::create_directories(from);
    bb::WriteAllText((from / L"settings.ini").wstring(), L"Language=de\n");

    CHECK(bb::OldName::MoveFolder(from.wstring(), to.wstring()));
    CHECK(!fs::exists(from));
    CHECK(bb::ReadAllText((to / L"settings.ini").wstring()) == L"Language=de\n");

    CHECK(!bb::OldName::MoveFolder(from.wstring(), to.wstring()));   // (nothing left to move)
    fs::create_directories(from);
    bb::WriteAllText((from / L"settings.ini").wstring(), L"Language=fi\n");
    CHECK(!bb::OldName::MoveFolder(from.wstring(), to.wstring()));   // (Softlid's own folder wins)
    CHECK(bb::ReadAllText((to / L"settings.ini").wstring()) == L"Language=de\n");
    fs::remove_all(root);
}
