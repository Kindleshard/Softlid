#include "features/reminders/Chime.h"
#include <windows.h>
#include <mmsystem.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#pragma comment(lib, "winmm.lib")

namespace bb::Chime {
namespace {

// E5 and B5, each fading out like a struck bell, the second starting a sixth of a second after the first.
std::vector<uint8_t> MakeWave(bool rising)
{
    constexpr int rate = 22050;
    const double notes[2] = {rising ? 659.25 : 987.77, rising ? 987.77 : 659.25};
    const int step = rate / 6, length = step * 2 + rate / 2;
    std::vector<int16_t> pcm(length);
    for (int i = 0; i < length; i++) {
        double v = 0;
        for (int n = 0; n < 2; n++) {
            if (i < n * step) continue;
            double t = (i - n * step) / double(rate);
            v += 6000 * std::fmin(1.0, t * 200) * std::exp(-t * 7) * std::sin(2 * 3.14159265358979 * notes[n] * t);
        }
        pcm[i] = static_cast<int16_t>(std::fmax(-32768.0, std::fmin(32767.0, v)));
    }
    // A minimal WAV file around it: mono, 16-bit.
    auto put32 = [](std::vector<uint8_t>& b, uint32_t v) { for (int k = 0; k < 4; k++) b.push_back(uint8_t(v >> (8 * k))); };
    auto put16 = [](std::vector<uint8_t>& b, uint16_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); };
    std::vector<uint8_t> wav;
    uint32_t dataBytes = length * 2;
    wav.insert(wav.end(), {'R', 'I', 'F', 'F'}); put32(wav, 36 + dataBytes);
    wav.insert(wav.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '}); put32(wav, 16);
    put16(wav, 1); put16(wav, 1); put32(wav, rate); put32(wav, rate * 2); put16(wav, 2); put16(wav, 16);
    wav.insert(wav.end(), {'d', 'a', 't', 'a'}); put32(wav, dataBytes);
    size_t at = wav.size();
    wav.resize(at + dataBytes);
    std::memcpy(wav.data() + at, pcm.data(), dataBytes);
    return wav;
}

} // namespace

void Play(bool rising)
{
    // Kept for the life of the app: Windows plays from this memory after Play returns.
    static std::vector<uint8_t> falling = MakeWave(false), risingWave = MakeWave(true);
    const std::vector<uint8_t>& wav = rising ? risingWave : falling;
    PlaySoundW(reinterpret_cast<LPCWSTR>(wav.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

} // namespace bb::Chime
