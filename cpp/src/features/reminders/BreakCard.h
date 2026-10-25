#pragma once
#include "core/Settings.h"
#include "platform/Window.h"
#include "ui/LayeredCanvas.h"
#include "ui/ScreenShade.h"
#include <string>

namespace bb {

// The break card: a countdown in the middle of the screen for a look-away or stand-up break, with Snooze and Skip
// (unless breaks are forced). It never takes focus, so typing isn't interrupted. When it is over it posts DoneMessage
// to its owner (wParam: 1 for a stand-up break, lParam: the snooze length in seconds, or 0) - the owner then
// deletes it.
class BreakCard : public Window {
public:
    static constexpr UINT DoneMessage = WM_APP + 2;

    // seconds/caption/note: a break of another length with its own words (the focus timer's rest, later).
    BreakCard(HWND owner, bool standUp, const Settings& s, int seconds = 0, std::wstring caption = {}, std::wstring note = {});

private:
    LRESULT OnMessage(UINT message, WPARAM w, LPARAM l) override;
    void Step();
    void Render();
    int ButtonAt(POINT p) const;   // 0 snooze, 1 skip, -1 none
    void Finish(int snoozeSeconds);
    std::wstring Format(int seconds) const;

    HWND owner;
    bool standUp, sounds, forced, over = false, finished = false;
    int total, left;
    int hover = -1, down = -1;
    float scale = 1;
    POINT at{};
    std::wstring title, hint;
    LayeredCanvas canvas;
    ScreenShade backdrop;
};

} // namespace bb
