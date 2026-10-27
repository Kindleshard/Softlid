#pragma once
#include "core/Settings.h"
#include "ui/DirectWindow.h"
#include "ui/controls/Basic.h"

namespace bb {

// The focus timer's rest, on the whole screen under the pointer: a green ring empties as the rest runs down, and a
// soft circle inside it breathes slowly (in, hold, out) with words to follow. Over it, the rounds done so far, and
// under it what comes next. "+5 min" makes the rest longer; Skip rest or Esc ends it (neither when breaks are
// forced). When it is over it posts BreakCard::DoneMessage to its owner; the owner then deletes it.
class FocusRest : public ui::DirectWindow {
public:
    FocusRest(HWND owner, const Settings& s, int round);
    ~FocusRest() override;

    float Progress() const;   // how far into the rest (0..1)
    int SecondsLeft() const;

protected:
    bool Shortcut(UINT key) override;
    LRESULT OnMessage(UINT message, WPARAM w, LPARAM l) override;
    void Layout() override;

private:
    void Frame();
    void Finish();
    double Now() const;

    class Scene;
    friend class Scene;
    HWND owner;
    bool sounds, forced;
    int round, workMinutes;
    double total, closeAt = -1;   // seconds of rest (longer with +5 min); when it started closing
    bool done = false;
    int shownCue = -1;
    Scene* scene = nullptr;
    ui::Button *more = nullptr, *skip = nullptr;
    LARGE_INTEGER clockStart{}, frequency{};
};

} // namespace bb
