#pragma once
#include <sapi.h>
#include <wrl/client.h>
#include <utility>
#include <string>
#include <vector>

namespace bb {

// Reads a guided break's lines aloud in a language ("tr"). Recorded clips (voice\<language>\<key>.mp3 or .wav next to
// Softlid.exe) are used when present, played through Windows' media control interface so they can overlap the
// chime; otherwise a Windows voice for that language speaks the text (and with none, nothing is said: the screen
// still shows the steps).
class Voice {
public:
    explicit Voice(std::wstring language) : language(std::move(language)) {}
    ~Voice();
    Voice(const Voice&) = delete;
    Voice& operator=(const Voice&) = delete;

    void Say(const std::wstring& key, const std::wstring& text);
    void Stop();

    static constexpr double Pause = 0.08;   // s between lines; the clips carry their own quiet at both ends too

    // How long a clip plays, in seconds (0 when there is none), measured once from the file itself.
    static double Length(const std::wstring& language, const std::wstring& key);
    // The shortest break, in whole seconds, that fits every line with a pause between them; 0 without clips.
    static int MinimumSeconds(const std::wstring& language, const std::vector<std::wstring>& keys);
    // The look-away break's lines, in the order they are spoken.
    static const std::vector<std::wstring>& LookAwayKeys();

private:
    static std::wstring Clip(const std::wstring& language, const std::wstring& key);
    bool Speaker();
    std::wstring language;
    Microsoft::WRL::ComPtr<ISpVoice> speaker;
    bool noSpeaker = false;   // no Windows voice for this language
    bool open = false;
};

} // namespace bb
