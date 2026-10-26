#pragma once
#include "core/Clock.h"
#include "core/Settings.h"
#include "features/update/Updater.h"
#include <functional>
#include <string>

namespace bb {

// The update check as the app runs it: by hand from Options, quietly once a week, and installing a newer version.
// It keeps what the Options page shows. The checking and installing themselves are given to it (the app hands it the
// updater; tests hand it fakes), and after each result it says what the app should do.
class Updates {
public:
    using CheckFn = std::function<void()>;
    using InstallFn = std::function<void(const Updater::Release&)>;
    Updates(CheckFn check, InstallFn install) : check(std::move(check)), install(std::move(install)) {}

    const Updater::State& State() const { return state; }

    void Check();                                            // by hand (nothing while one is under way)
    void Install();                                          // the newer version found
    void Weekly(const Settings& s, const LocalTime& today);  // once a week, if allowed; quiet unless something is found
                                                             // (called only when there is a release channel)

    // What the app does after a result.
    struct Effects {
        bool refresh = false;            // the Options page shows something new
        bool saveSettings = false;       // LastUpdateCheck changed
        std::wstring notifyTitle, notifyText;   // a newer version found by the weekly check: say so from the tray
        bool restart = false;            // the new version is in place: start it and quit
    };
    Effects Received(const Updater::Result& r, Settings& s, const LocalTime& today);

private:
    CheckFn check;
    InstallFn install;
    Updater::State state;
    bool quiet = false;   // the check under way is the weekly one
};

} // namespace bb
