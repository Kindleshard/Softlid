#include "features/update/Updates.h"
#include "core/Lang.h"

namespace bb {

void Updates::Check()
{
    if (state.busy) return;
    state.busy = true;
    state.look = Updater::State::Look::Plain;
    state.text = Lang::T(L"Checking...");
    quiet = false;
    check();
}

void Updates::Install()
{
    if (state.busy || !state.release) return;
    state.busy = state.downloading = true;
    state.progress = 0;
    install(*state.release);
}

void Updates::Weekly(const Settings& s, const LocalTime& today)
{
    if (!s.UpdateCheck || state.busy || state.release) return;
    int y, m, d;
    if (ParseIsoDate(s.LastUpdateCheck, y, m, d) && DayNumber(today.year, today.month, today.day) - DayNumber(y, m, d) < 7) return;
    state.busy = quiet = true;
    check();
}

Updates::Effects Updates::Received(const Updater::Result& r, Settings& s, const LocalTime& today)
{
    using Kind = Updater::Result::Kind;
    using Look = Updater::State::Look;
    Effects fx;
    fx.refresh = true;
    switch (r.kind) {
    case Kind::Progress:
        state.progress = r.progress;
        state.look = Look::Newer;
        state.text = r.text;
        break;
    case Kind::Ready:
        state.text = r.text;
        fx.restart = true;
        fx.refresh = false;
        break;
    case Kind::Failed:
        state.busy = state.downloading = false;
        state.look = Look::Plain;
        state.text = r.text;
        break;
    default:   // the end of a check
        state.busy = false;
        if (quiet) {
            s.LastUpdateCheck = IsoDate(today);
            fx.saveSettings = true;
        }
        if (r.kind == Kind::Newer) {
            state.release = r.release;
            state.look = Look::Newer;
            state.text = Lang::F(L"Update available: version {0}", {r.release.tag});
            if (quiet) {
                fx.notifyTitle = Lang::F(L"Softlid {0} is available", {r.release.tag});
                fx.notifyText = Lang::T(L"Open Softlid options to install it.");
            }
        } else if (!quiet) {
            state.look = r.kind == Kind::UpToDate ? Look::UpToDate : Look::Plain;
            state.text = r.text;
        }
        quiet = false;
    }
    return fx;
}

} // namespace bb
