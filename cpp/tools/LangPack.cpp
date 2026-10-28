// Run by the build: compresses each translation in res\lang (the same way LanguageText unpacks them) and writes
// lang.rc, which app.rc includes, naming each one LANG_<CODE>.
//   LangPack <folder of xx.txt> <folder to write to>
#include <windows.h>
#include <compressapi.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#pragma comment(lib, "cabinet.lib")

namespace fs = std::filesystem;

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3) {
        fwprintf(stderr, L"LangPack <folder of xx.txt> <folder to write to>\n");
        return 2;
    }
    fs::path from = argv[1], to = argv[2];
    fs::create_directories(to);
    COMPRESSOR_HANDLE packer = nullptr;
    if (!CreateCompressor(COMPRESS_ALGORITHM_MSZIP, nullptr, &packer)) return 1;
    std::string rc = "// Made by LangPack from res\\lang: each translation, compressed.\n";
    size_t before = 0, after = 0;
    for (const auto& entry : fs::directory_iterator(from)) {
        if (entry.path().extension() != L".txt") continue;
        std::ifstream in(entry.path(), std::ios::binary);
        std::vector<char> text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        SIZE_T needed = 0;
        Compress(packer, text.data(), text.size(), nullptr, 0, &needed);
        std::vector<char> packed(needed);
        if (!Compress(packer, text.data(), text.size(), packed.data(), packed.size(), &needed)) return 1;
        packed.resize(needed);
        fs::path out = to / entry.path().filename().replace_extension(L".bin");
        std::ofstream(out, std::ios::binary).write(packed.data(), static_cast<std::streamsize>(packed.size()));
        std::string code = entry.path().stem().string();
        for (char& c : code) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
        rc += "LANG_" + code + " RCDATA \"" + out.generic_string() + "\"\n";
        before += text.size();
        after += packed.size();
    }
    CloseCompressor(packer);
    std::ofstream(to / L"lang.rc", std::ios::binary) << rc;
    wprintf(L"LangPack: translations %zu KB, packed %zu KB\n", before / 1024, after / 1024);
    return 0;
}
