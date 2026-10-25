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

uint32_t BigEndian(const std::string& b, size_t at)
{
    auto u = [&](size_t i) { return static_cast<uint32_t>(static_cast<unsigned char>(b[at + i])); };
    return u(0) << 24 | u(1) << 16 | u(2) << 8 | u(3);
}

uint32_t LittleEndian(const std::string& b, size_t at)
{
    auto u = [&](size_t i) { return static_cast<uint32_t>(static_cast<unsigned char>(b[at + i])); };
    return u(0) | u(1) << 8 | u(2) << 16 | u(3) << 24;
}

// An MP3's length from its headers: the frame count in a Xing/Info or VBRI header, or (constant bit rate) the size
// over the bit rate. (Asking Windows' media player instead loads its whole playback stack, several MB that stay.)
double Mp3Seconds(const std::string& b)
{
    size_t n = b.size(), at = 0;
    auto byte = [&](size_t i) { return static_cast<unsigned char>(b[i]); };
    if (n >= 10 && b.compare(0, 3, "ID3") == 0)   // a tag first: its size is in 7-bit bytes
        at = 10 + (byte(6) << 21 | byte(7) << 14 | byte(8) << 7 | byte(9)) + ((byte(5) & 0x10) ? 10 : 0);
    static const int rates1[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
    static const int rates2[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
    static const int samples[3][3] = {{11025, 12000, 8000}, {22050, 24000, 16000}, {44100, 48000, 32000}};   // 2.5, 2, 1
    for (; at + 4 <= n; at++) {
        if (byte(at) != 0xFF || (byte(at + 1) & 0xE0) != 0xE0) continue;
        int version = (byte(at + 1) >> 3) & 3, layer = (byte(at + 1) >> 1) & 3, rate = byte(at + 2) >> 4, sample = (byte(at + 2) >> 2) & 3;
        if (version == 1 || layer != 1 || rate == 0 || rate == 15 || sample == 3) continue;   // only Layer III
        bool mpeg1 = version == 3, mono = (byte(at + 3) >> 6) == 3;
        int hz = samples[mpeg1 ? 2 : version == 2 ? 1 : 0][sample], perFrame = mpeg1 ? 1152 : 576;
        size_t xing = at + 4 + (mpeg1 ? (mono ? 17 : 32) : (mono ? 9 : 17));
        if (xing + 12 <= n && (b.compare(xing, 4, "Xing") == 0 || b.compare(xing, 4, "Info") == 0) && (BigEndian(b, xing + 4) & 1))
            return (BigEndian(b, xing + 8) + 1.0) * perFrame / hz;   // (+ the header frame itself, played as silence)
        size_t vbri = at + 4 + 32;
        if (vbri + 18 <= n && b.compare(vbri, 4, "VBRI") == 0) return static_cast<double>(BigEndian(b, vbri + 14)) * perFrame / hz;
        size_t end = n >= 128 && b.compare(n - 128, 3, "TAG") == 0 ? n - 128 : n;
        return (end - at) * 8.0 / ((mpeg1 ? rates1 : rates2)[rate] * 1000.0);
    }
    return 0;
}

// A WAV's length: its data size over its bytes per second.
double WavSeconds(const std::string& b)
{
    if (b.size() < 12 || b.compare(0, 4, "RIFF") != 0 || b.compare(8, 4, "WAVE") != 0) return 0;
    uint32_t byteRate = 0;
    for (size_t at = 12; at + 8 <= b.size();) {
        uint32_t size = LittleEndian(b, at + 4);
        if (b.compare(at, 4, "fmt ") == 0 && at + 16 <= b.size()) byteRate = LittleEndian(b, at + 16);
        if (b.compare(at, 4, "data") == 0) return byteRate ? static_cast<double>(size) / byteRate : 0;
        at += 8 + size + (size & 1);
    }
    return 0;
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
    if (!clip.empty()) {
        std::string bytes = ReadAllBytes(clip).value_or("");
        s = clip.ends_with(L".wav") ? WavSeconds(bytes) : Mp3Seconds(bytes);
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
