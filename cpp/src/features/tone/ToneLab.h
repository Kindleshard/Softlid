#pragma once
#include "ui/DirectWindow.h"
#include "ui/controls/Basic.h"
#include "ui/controls/Choices.h"
#include "ui/controls/Pickers.h"
#include <string>

namespace bb {

class ToneLabHost;

// A place to judge every engine at every strength on the same picture, and write down what you see. Each rating is
// a line in %AppData%\Softlid\tone-feedback.txt. The lab changes the real settings while it is open (the engines
// work on the whole screen); App puts them back when it closes.
class ToneLab : public ui::DirectWindow {
public:
    explicit ToneLab(ToneLabHost& app);
    void Start(HICON icon);
    static std::wstring FeedbackFile();

protected:
    bool Shortcut(UINT key) override;
    LRESULT OnMessage(UINT message, WPARAM w, LPARAM l) override;

private:
    void ShowCombo(int engine, int level);   // puts this engine and strength on screen, with fresh answers
    void UpdateProgress(const std::wstring& message);
    int Index() const { return engine->Selected() * 4 + level->Selected(); }
    void Save();
    void Next();

    ToneLabHost& app;
    ui::Segmented *engine = nullptr, *level = nullptr;
    ui::Segmented* answers[5] = {};
    ui::TextBox* note = nullptr;
    ui::Label *progress = nullptr, *brightText = nullptr;
    bool rated[16] = {};
    bool switching = false;
};

} // namespace bb
