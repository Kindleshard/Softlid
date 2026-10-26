#include "features/update/Updater.h"
#include "core/Lang.h"
#include "core/Version.h"
#include "platform/Files.h"
#include "platform/Text.h"
#include "platform/Window.h"
#include <urlmon.h>
#include <cstdio>
#include <regex>
#include <thread>
#include <vector>

#pragma comment(lib, "urlmon.lib")

namespace bb::Updater {
namespace {

void Post(HWND window, UINT message, Result r) { PostOwned(window, message, 0, std::move(r)); }

Result Note(Result::Kind kind, std::wstring text)
{
    Result r{kind, std::move(text)};
    return r;
}

// Downloads always fresh (never from the browser cache: an old answer would hide a new release), reporting progress.
class Fetch : public IBindStatusCallback {
public:
    using Progress = void (*)(void* context, ULONG done, ULONG total);
    Fetch(Progress progress = nullptr, void* context = nullptr) : progress(progress), context(context) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override
    {
        if (id == IID_IUnknown || id == IID_IBindStatusCallback) {
            *out = static_cast<IBindStatusCallback*>(this);
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }    // (lives on the caller's stack)
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE OnStartBinding(DWORD, IBinding*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetPriority(LONG*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE OnLowResource(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnProgress(ULONG done, ULONG total, ULONG status, LPCWSTR) override
    {
        if (progress && status == BINDSTATUS_DOWNLOADINGDATA) progress(context, done, total);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnStopBinding(HRESULT, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetBindInfo(DWORD* flags, BINDINFO*) override
    {
        *flags = BINDF_GETNEWESTVERSION | BINDF_NOWRITECACHE | BINDF_PRAGMA_NO_CACHE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDataAvailable(DWORD, DWORD, FORMATETC*, STGMEDIUM*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnObjectAvailable(REFIID, IUnknown*) override { return S_OK; }

private:
    Progress progress;
    void* context;
};

// "1.2.10-beta" -> {1, 2, 10}; false if it doesn't start with a number.
bool ParseVersion(const std::wstring& text, int (&out)[3])
{
    out[0] = out[1] = out[2] = 0;
    size_t at = 0;
    for (int i = 0; i < 3 && at < text.size() && iswdigit(text[at]); i++) {
        out[i] = _wtoi(text.c_str() + at);
        while (at < text.size() && iswdigit(text[at])) at++;
        if (at < text.size() && text[at] == L'.') at++;
        else break;
    }
    return !text.empty() && iswdigit(text[0]);
}

bool Newer(const int (&a)[3], const int (&b)[3])
{
    for (int i = 0; i < 3; i++)
        if (a[i] != b[i]) return a[i] > b[i];
    return false;
}

bool LooksLikeProgram(const std::wstring& path)
{
    auto bytes = ReadAllBytes(path);
    return bytes && bytes->size() >= 64 * 1024 && (*bytes)[0] == 'M' && (*bytes)[1] == 'Z';
}

std::wstring Mb(ULONG bytes)
{
    wchar_t text[16];
    swprintf_s(text, L"%.1f", bytes / 1048576.0);
    return text;
}

void CheckNow(HWND window, UINT message)
{
    std::wstring json = TempFolder() + L"BlinkBuddy-release.json";
    Fetch fetch;
    std::wstring url = L"https://api.github.com/repos/" + std::wstring(ReleaseRepo) + L"/releases/latest";
    HRESULT hr = URLDownloadToFileW(nullptr, url.c_str(), json.c_str(), 0, &fetch);
    std::string text = ReadAllBytes(json).value_or("");
    DeleteFileW(json.c_str());
    if (FAILED(hr) || text.empty()) {
        wchar_t code[16];
        swprintf_s(code, L"0x%08X", static_cast<unsigned>(hr));
        return Post(window, message, Note(Result::Kind::Note, Lang::F(L"Couldn't check for updates: {0}", {code})));
    }
    std::smatch tag, exe, page;
    int latest[3];
    if (!std::regex_search(text, tag, std::regex("\"tag_name\"\\s*:\\s*\"v?([0-9][^\"]*)\"")) || !ParseVersion(FromUtf8(tag[1].str()), latest))
        return Post(window, message, Note(Result::Kind::Note, Lang::T(L"Couldn't read the latest release's version.")));
    if (!Newer(latest, VersionParts)) return Post(window, message, Note(Result::Kind::UpToDate, Lang::T(L"You're up to date.")));
    Result r{Result::Kind::Newer};
    r.release.tag = FromUtf8(tag[1].str());
    if (std::regex_search(text, exe, std::regex("\"browser_download_url\"\\s*:\\s*\"([^\"]+/BlinkBuddy\\.exe)\"")))
        r.release.download = FromUtf8(exe[1].str());
    r.release.page = std::regex_search(text, page, std::regex("\"html_url\"\\s*:\\s*\"([^\"]+/releases/[^\"]+)\""))
                         ? FromUtf8(page[1].str())
                         : L"https://github.com/" + std::wstring(ReleaseRepo) + L"/releases/latest";
    r.text = Lang::F(L"Version {0} is available (you have {1}).", {r.release.tag, Version});
    Post(window, message, std::move(r));
}

struct InstallContext {
    HWND window;
    UINT message;
    std::wstring tag;
    ULONGLONG lastPost = 0;
};

void InstallNow(const Release& r, HWND window, UINT message)
{
    std::wstring exe = ExePath(), fresh = exe + L".new", old = exe + L".old";
    InstallContext context{window, message, r.tag};
    Fetch fetch([](void* c, ULONG done, ULONG total) {
        auto* ctx = static_cast<InstallContext*>(c);
        ULONGLONG now = GetTickCount64();
        if (now - ctx->lastPost < 100 && done < total) return;   // (a few updates a second is plenty)
        ctx->lastPost = now;
        Result p{Result::Kind::Progress, Lang::F(L"Downloading {0}: {1} of {2} MB", {ctx->tag, Mb(done), Mb(total)})};
        p.progress = total > 0 ? static_cast<float>(done) / total : 0;
        Post(ctx->window, ctx->message, std::move(p));
    }, &context);
    auto fail = [&](const std::wstring& why) {
        DeleteFileW(fresh.c_str());
        Post(window, message, Note(Result::Kind::Failed, Lang::F(L"Couldn't install the update: {0}", {why})));
    };
    HRESULT hr = URLDownloadToFileW(nullptr, r.download.c_str(), fresh.c_str(), 0, &fetch);
    if (FAILED(hr)) {
        wchar_t code[16];
        swprintf_s(code, L"0x%08X", static_cast<unsigned>(hr));
        return fail(Lang::F(L"the download failed ({0})", {code}));
    }
    if (!LooksLikeProgram(fresh)) return fail(Lang::T(L"the download isn't a Windows program"));
    Post(window, message, Note(Result::Kind::Progress, Lang::T(L"Installing...")));
    DeleteFileW(old.c_str());
    if (!MoveFileW(exe.c_str(), old.c_str())) return fail(Lang::F(L"BlinkBuddy.exe couldn't be moved (error {0})", {std::to_wstring(GetLastError())}));
    if (!MoveFileW(fresh.c_str(), exe.c_str())) {
        DWORD error = GetLastError();
        MoveFileW(old.c_str(), exe.c_str());   // put this copy back if the new one can't take its place
        return fail(Lang::F(L"the new BlinkBuddy.exe couldn't be put in place (error {0})", {std::to_wstring(error)}));
    }
    Post(window, message, Note(Result::Kind::Ready, Lang::T(L"Restarting...")));
}

} // namespace

bool Available() { return ReleaseRepo[0] != 0; }

void Check(HWND window, UINT message)
{
    if (!Available()) {
        Post(window, message, Note(Result::Kind::Note, Lang::T(L"Updates aren't available yet: this copy of BlinkBuddy has no release channel.")));
        return;
    }
    std::thread([window, message] {
        HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        CheckNow(window, message);
        if (SUCCEEDED(com)) CoUninitialize();
    }).detach();
}

void Install(const Release& r, HWND window, UINT message)
{
    if (r.download.empty()) {
        Post(window, message, Note(Result::Kind::Failed, Lang::F(L"This release has no BlinkBuddy.exe to install. Get it from {0}", {r.page})));
        return;
    }
    std::thread([r, window, message] {
        HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        InstallNow(r, window, message);
        if (SUCCEEDED(com)) CoUninitialize();
    }).detach();
}

void StartNewCopy()
{
    std::wstring exe = ExePath();
    std::wstring command = L"\"" + exe + L"\" --tray --after " + std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

void AfterUpdate(const wchar_t* commandLine)
{
    const wchar_t* after = wcsstr(commandLine, L"--after ");
    if (!after) return;
    DWORD pid = static_cast<DWORD>(_wtoi(after + 8));
    if (HANDLE old = OpenProcess(SYNCHRONIZE, FALSE, pid)) {
        WaitForSingleObject(old, 15000);
        CloseHandle(old);
    }
    std::wstring exe = ExePath() + L".old";
    for (int i = 0; i < 10 && !DeleteFileW(exe.c_str()) && GetLastError() == ERROR_ACCESS_DENIED; i++) Sleep(300);
}

} // namespace bb::Updater
