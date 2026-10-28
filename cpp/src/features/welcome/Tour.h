#pragma once

namespace bb {

class SettingsHost;

// The first-run tour: four short steps (welcome and the language; the features worth having; where BlinkBuddy lives;
// done, with a break to try), each read aloud in the interface language unless muted. It shows once, on the first
// start (TourDone), and again from Options. Only one is open at a time.
namespace Tour {

void Open(SettingsHost& app);

} // namespace Tour
} // namespace bb
