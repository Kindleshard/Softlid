#include "features/tone/LutFiles.h"
#include "core/Json.h"
#include "core/ToneCurve.h"
#include "platform/Files.h"
#include "platform/Text.h"
#include "platform/Window.h"
#include <shlobj.h>
#include <urlmon.h>
#include <string>
#include <thread>

#pragma comment(lib, "urlmon.lib")

namespace bb::LutFiles {
namespace {

constexpr wchar_t Releases[] = L"https://api.github.com/repos/sup3rflyer/DesktopLUT/releases/latest";

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
    std::wstring json = TempFolder() + L"Softlid-DesktopLUT.json", zip = TempFolder() + L"Softlid-DesktopLUT.zip";
    auto fail = [&](const std::wstring& why) {
        DeleteFileW(json.c_str());
        DeleteFileW(zip.c_str());
        PostOwned<std::wstring>(window, message, 2, why);
    };
    PostOwned<std::wstring>(window, message, 0, L"Looking up the latest DesktopLUT release...");
    if (FAILED(URLDownloadToFileW(nullptr, Releases, json.c_str(), 0, nullptr)))
        return fail(L"Couldn't reach GitHub. Get it from " + std::wstring(Page) + L" and pick it with Browse.");
    std::string text = ReadAllBytes(json).value_or("");
    std::string url = JsonFirst(text, "browser_download_url", [](std::string_view u) { return u.size() > 4 && u.ends_with(".zip"); });
    if (url.empty())
        return fail(L"The latest release has no download Softlid recognises. Get it from " + std::wstring(Page) + L" and pick it with Browse.");
    std::string tag = JsonFirst(text, "tag_name", [](std::string_view t) { return !t.empty(); });
    std::wstring version = tag.empty() ? L"latest" : FromUtf8(tag);
    PostOwned<std::wstring>(window, message, 0, L"Downloading DesktopLUT " + version + L"...");
    if (FAILED(URLDownloadToFileW(nullptr, FromUtf8(url).c_str(), zip.c_str(), 0, nullptr)))
        return fail(L"The download didn't finish. Get it from " + std::wstring(Page) + L" and pick it with Browse.");

    // Unpacking: only the program itself, with the tar that comes with Windows (it reads zip files).
    PostOwned<std::wstring>(window, message, 0, L"Unpacking...");
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
        return PostOwned<std::wstring>(window, message, 2,
                    L"The download didn't contain DesktopLUT.exe and DwmHook.dll at its top. Get it from " + std::wstring(Page) + L" and pick it with Browse.");
    PostOwned<std::wstring>(window, message, 1, L"DesktopLUT " + version + L" is set up.");
}

} // namespace

std::wstring CubePath() { return RoamingAppData() + L"\\Softlid\\DesktopLUT.cube"; }

std::wstring Folder()
{
    wchar_t* local = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) path = local;
    CoTaskMemFree(local);
    return path + L"\\Softlid\\DesktopLUT";
}

bool WriteLevel(const std::wstring& path, int level)
{
    constexpr int size = 33;   // the usual size for desktop LUTs, and quick to write
    std::string out;
    out.reserve(size * size * size * 26 + 256);
    std::string title = level < 0 ? "unchanged" : ToUtf8(ToneCurve::Levels[level]);
    out += "TITLE \"Softlid " + title + "\"\n";
    out += "# Colour-preserving tone map from Softlid. Input and output are display (sRGB-encoded) values.\n";
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
