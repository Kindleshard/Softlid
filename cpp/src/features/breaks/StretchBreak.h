#pragma once
#include "core/Settings.h"
#include "platform/Voice.h"
#include "ui/DirectWindow.h"
#include "ui/controls/Basic.h"
#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace bb {

// A stand-up break with a coach: a stick figure on a dark full screen shows each stretch (the usual low-risk
// desk-break routine), morphing smoothly from one to the next. It stands up from a seat at the start and waves at the
// end; a spoken line (when on) introduces each stretch and is never cut. Esc or Skip ends it, Snooze waits 5 min
// (neither when breaks are forced). When it is over it posts BreakCard::DoneMessage to its owner (a stand-up break,
// with the snooze length); the owner then deletes it.
class StretchBreak : public ui::DirectWindow {
public:
    StretchBreak(HWND owner, const Settings& s);
    ~StretchBreak() override;

    // Joints, in body units (y up, floor at 0, about 1.75 tall).
    enum Joint { Head, Neck, ShL, ShR, ElL, ElR, HaL, HaR, Pelvis, HipL, HipR, KnL, KnR, FtL, FtR, Toe, ToeR, JointCount };
    using Pose = std::array<D2D1_POINT_2F, JointCount>;

    struct Stretch { const wchar_t* key; const wchar_t* title; const wchar_t* text; Pose (*pose)(double); };

protected:
    bool Shortcut(UINT key) override;
    LRESULT OnMessage(UINT message, WPARAM w, LPARAM l) override;
    void Layout() override;

private:
    void Frame();
    void Finish(int snoozeSeconds);
    double Now() const;

    class Coach;
    friend class Coach;
    HWND owner;
    bool sounds, forced;
    std::unique_ptr<Voice> voice;
    std::vector<Stretch> plan;   // the intro, then the stretches
    std::vector<double> starts;  // when each starts, and (last) when the last one ends
    double lead, outroLen;
    Coach* coach = nullptr;
    ui::Button *skip = nullptr, *later = nullptr;
    Pose pose{}, from{};
    bool hasFrom = false;
    int current = -2, spoken = -1, shownSeconds = -1, snooze = 0;
    double blendStart = 0, outroAt = -1, closeAt = -1;
    bool done = false;
    LARGE_INTEGER clockStart{}, frequency{};
};

} // namespace bb
