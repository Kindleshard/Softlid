#include "features/tone/LutFiles.h"
#include "core/ToneCurve.h"
#include "platform/Files.h"
#include "platform/Text.h"
#include <shlobj.h>
#include <urlmon.h>
#include <regex>
#include <string>
#include <thread>

#pragma comment(lib, "urlmon.lib")

namespace bb::LutFiles {
namespace {

constexpr wchar_t Releases[] = L"https://api.github.com/repos/sup3rflyer/DesktopLUT/releases/latest";

void Post(HWND window, UINT message, WPARAM kind, const std::wstring& text)
{
    auto* line = new std::wstring(text);
    if (!PostMessageW(window, message, kind, reinterpret_cast<LPARAM>(line))) delete line;
}

std::string ReadText(const std::wstring& path) { return ReadAllBytes(path).value_or(""); }

// Runs a program and waits for it (hidden); true if it ran and ended with 0.
bool RunHidden(std::wstring command)
{
    STARTUPINFOW si{sizeof si};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return false;
    WaitForSingleObject(pi.hProcess, 60000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0;
}

void InstallNow(HWND window, UINT message)
{
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    std::wstring json = std::wstring(temp) + L"BlinkBuddy-DesktopLUT.json", zip = std::wstring(temp) + L"BlinkBuddy-DesktopLUT.zip";
    auto fail = [&](const std::wstring& why) {
        DeleteFileW(json.c_str());
        DeleteFileW(zip.c_str());
        Post(window, message, 2, why);
    };
    Post(window, message, 0, L"Looking up the latest DesktopLUT release...");
    if (FAILED(URLDownloadToFileW(nullptr, Releases, json.c_str(), 0, nullptr)))
        return fail(L"Couldn't reach GitHub. Get it from " + std::wstring(Page) + L" and pick it with Browse.");
    std::string text = ReadText(json);
    std::smatch url, tag;
    if (!std::regex_search(text, url, std::regex("\"browser_download_url\"\\s*:\\s*\"([^\"]+\\.zip)\"")))
        return fail(L"The latest release has no download BlinkBuddy recognises. Get it from " + std::wstring(Page) + L" and pick it with Browse.");
    std::wstring version = std::regex_search(text, tag, std::regex("\"tag_name\"\\s*:\\s*\"([^\"]+)\"")) ? FromUtf8(tag[1].str()) : L"latest";
    Post(window, message, 0, L"Downloading DesktopLUT " + version + L"...");
    if (FAILED(URLDownloadToFileW(nullptr, FromUtf8(url[1].str()).c_str(), zip.c_str(), 0, nullptr)))
        return fail(L"The download didn't finish. Get it from " + std::wstring(Page) + L" and pick it with Browse.");

    // Unpacking: only the program itself, with the tar that comes with Windows (it reads zip files).
    Post(window, message, 0, L"Unpacking...");
    std::wstring folder = Folder();
    SHCreateDirectoryExW(nullptr, folder.c_str(), nullptr);
    wchar_t system[MAX_PATH];
    GetSystemDirectoryW(system, MAX_PATH);
    RunHidden(L"\"" + std::wstring(system) + L"\\tar.exe\" -xf \"" + zip + L"\" -C \"" + folder + L"\" --strip-components=0 DesktopLUT.exe DwmHook.dll");
    bool exe = GetFileAttributesW((folder + L"\\DesktopLUT.exe").c_str()) != INVALID_FILE_ATTRIBUTES;
    bool hook = GetFileAttributesW((folder + L"\\DwmHook.dll").c_str()) != INVALID_FILE_ATTRIBUTES;
    if (!exe || !hook) {
        // (In a folder inside the zip: look for them anywhere in it.)
        RunHidden(L"\"" + std::wstring(system) + L"\\tar.exe\" -xf \"" + zip + L"\" -C \"" + folder + L"\" --include=*DesktopLUT.exe --include=*DwmHook.dll");
    }
    DeleteFileW(json.c_str());
    DeleteFileW(zip.c_str());
    if (GetFileAttributesW((folder + L"\\DesktopLUT.exe").c_str()) == INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesW((folder + L"\\DwmHook.dll").c_str()) == INVALID_FILE_ATTRIBUTES)
        return Post(window, message, 2,
                    L"The download didn't contain DesktopLUT.exe and DwmHook.dll at its top. Get it from " + std::wstring(Page) + L" and pick it with Browse.");
    Post(window, message, 1, L"DesktopLUT " + version + L" is set up.");
}

} // namespace

std::wstring CubePath() { return RoamingAppData() + L"\\BlinkBuddy\\DesktopLUT.cube"; }

std::wstring Folder()
{
    wchar_t* local = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) path = local;
    CoTaskMemFree(local);
    return path + L"\\BlinkBuddy\\DesktopLUT";
}

bool WriteLevel(const std::wstring& path, int level)
{
    constexpr int size = 33;   // the usual size for desktop LUTs, and quick to write
    std::string out;
    out.reserve(size * size * size * 26 + 256);
    std::string title = level < 0 ? "unchanged" : ToUtf8(ToneCurve::Levels[level]);
    out += "TITLE \"BlinkBuddy " + title + "\"\n";
    out += "# Colour-preserving tone map from BlinkBuddy. Input and output are display (sRGB-encoded) values.\n";
    out += "LUT_3D_SIZE " + std::to_string(size) + "\nDOMAIN_MIN 0.0 0.0 0.0\nDOMAIN_MAX 1.0 1.0 1.0\n";
    char line[64];
    for (int bi = 0; bi < size; bi++)   // blue changes slowest, red fastest (the .cube convention)
        for (int gi = 0; gi < size; gi++)
            for (int ri = 0; ri < size; ri++) {
                double r, g, b;
                ToneCurve::Map(ri / (size - 1.0), gi / (size - 1.0), bi / (size - 1.0), level, r, g, b);
                snprintf(line, sizeof line, "%.6f %.6f %.6f\n", r, g, b);   // (always a point: the C runtime's "C" locale)
                out += line;
            }
    std::wstring folder = path.substr(0, path.find_last_of(L'\\'));
    SHCreateDirectoryExW(nullptr, folder.c_str(), nullptr);
    return WriteAllBytesSafely(path, std::string_view(out));
}

void Install(HWND window, UINT message)
{
    std::thread([window, message] {
        HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        InstallNow(window, message);
        if (SUCCEEDED(com)) CoUninitialize();
    }).detach();
}

} // namespace bb::LutFiles
