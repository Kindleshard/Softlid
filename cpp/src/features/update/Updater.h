#pragma once
#include <windows.h>
#include <optional>
#include <string>

namespace bb::Updater {

// Checking for a newer Softlid: asks GitHub for the latest release of ReleaseRepo, compares its tag ("v1.2.0")
// with this copy's version and, if asked, downloads the release's Softlid.exe and swaps it in. A running program
// can be renamed but not overwritten, so this copy steps aside as Softlid.exe.old, the new one takes its name,
// starts (waiting for this one to quit first) and tidies the old file away.

bool Available();   // a release channel is set

struct Release {
    std::wstring tag, download, page;   // download: empty if the release has no Softlid.exe
};

// How the update check stands, for the Options page.
struct State {
    enum class Look { Plain, UpToDate, Newer } look = Look::Plain;   // dim; green with a thumbs-up; orange
    std::wstring text;                     // already translated (empty: not checked yet)
    float progress = 0;
    bool busy = false, downloading = false;
    std::optional<Release> release;        // a newer version, once found
};

// What a worker thread reports, posted to `window` as `message` with lParam a heap Result* (the receiver deletes it).
struct Result {
    enum class Kind { Note, UpToDate, Newer, Progress, Ready, Failed } kind;
    std::wstring text;        // already translated
    Release release;          // Newer
    float progress = 0;       // Progress (0..1)
};

void Check(HWND window, UINT message);                          // Note, UpToDate or Newer
void Install(const Release& r, HWND window, UINT message);      // Progress..., then Ready or Failed
void StartNewCopy();                                            // after Ready: the new exe starts (and waits for this one)
void AfterUpdate(const wchar_t* commandLine);                   // at startup with --after <pid>: wait for it, tidy up

} // namespace bb::Updater
