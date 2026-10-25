#include "platform/Voice.h"
#include "platform/Files.h"
#include <windows.h>
#include <mmsystem.h>
#include <cmath>
#include <map>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "sapi.lib")

namespace bb {
namespace {

constexpr wchar_t Alias[] = L"blinkbuddyvoice";

int Mci(const std::wstring& command, wchar_t* result = nullptr, UINT length = 0)
{
    return static_cast<int>(mciSendStringW(command.c_str(), result, length, nullptr));
}

} // namespace

Voice::~Voice() { Stop(); }

const std::vector<std::wstring>& Voice::LookAwayKeys()
{
    static const std::vector<std::wstring> keys{L"far", L"sides", L"updown", L"circle", L"close", L"done"};
    return keys;
}

std::wstring Voice::Clip(const std::wstring& key)
{
    for (const wchar_t* ext : {L".wav", L".mp3"}) {
        std::wstring path = ExeFolder() + L"\\voice\\" + key + ext;
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return path;
    }
    return L"";
}

double Voice::Length(const std::wstring& key)
{
    static std::map<std::wstring, double> lengths;
    if (auto found = lengths.find(key); found != lengths.end()) return found->second;
    double s = 0;
    std::wstring clip = Clip(key);
    if (!clip.empty() && Mci(L"open \"" + clip + L"\" type mpegvideo alias blinkbuddymeasure") == 0) {
        wchar_t result[32] = {};
        Mci(L"set blinkbuddymeasure time format milliseconds");
        Mci(L"status blinkbuddymeasure length", result, 32);
        Mci(L"close blinkbuddymeasure");
        s = _wtoi(result) / 1000.0;
    }
    lengths[key] = s;
    return s;
}

int Voice::MinimumSeconds(const std::vector<std::wstring>& keys)
{
    double total = 0;
    for (const auto& k : keys) {
        double l = Length(k);
        if (l <= 0) return 0;
        total += l;
    }
    return static_cast<int>(std::ceil(total + Pause * (keys.size() - 1)));
}

void Voice::Say(const std::wstring& key, const std::wstring& text)
{
    Stop();
    std::wstring clip = Clip(key);
    if (!clip.empty() && Mci(L"open \"" + clip + L"\" type mpegvideo alias " + Alias) == 0) {
        open = true;
        Mci(std::wstring(L"play ") + Alias);
        return;
    }
    if (!speaker) {
        if (FAILED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&speaker)))) return;   // no voices: the screen still shows the steps
        speaker->SetRate(-1);
    }
    speaker->Speak(text.c_str(), SPF_ASYNC | SPF_PURGEBEFORESPEAK | SPF_IS_NOT_XML, nullptr);
}

void Voice::Stop()
{
    if (open) {
        Mci(std::wstring(L"close ") + Alias);
        open = false;
    }
    if (speaker) speaker->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);
}

} // namespace bb
