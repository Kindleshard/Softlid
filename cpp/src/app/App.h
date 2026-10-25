#pragma once
#include "app/Tray.h"
#include "core/Reminders.h"
#include "core/Settings.h"
#include "features/reminders/BreakCard.h"
#include "features/reminders/EyePopup.h"
#include <windows.h>
#include <chrono>
#include <memory>

namespace bb {

// The tray app: a hidden window that owns the tray icon and gets the 1-second tick, the menu, and requests from a
// second copy to show the settings. Everything else (reminders, the screen filter, windows) hangs off this.
class App {
public:
    App(HINSTANCE instance, HANDLE showRequest, bool openSettings);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    int Run();            // the message loop, until Exit
    void Changed();       // settings changed: saved on the next tick

private:
    using Clock = std::chrono::system_clock;

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w, LPARAM l);
    LRESULT Handle(UINT message, WPARAM w, LPARAM l);

    void OnTick();
    void ShowMenu();
    void ShowSettings();
    void UpdateTrayTip();
    void Exit();

    // Reminders (see core/Reminders for when they fall due).
    void Blink(bool instant);
    void StartBreak(bool standUp);
    void BreakDone(bool standUp, int snoozeSeconds);
    std::wstring BreakStatus() const;
    std::wstring StandUpStatus() const;
    void Pause(int minutes);   // -1: until resumed
    bool Paused() const;

    HINSTANCE instance;
    HANDLE showRequest;   // set by a second copy of BlinkBuddy
    HWND window = nullptr;
    HICON icon = nullptr;
    UINT taskbarCreated = 0;
    Tray tray;
    Settings settings;
    bool dirty = false;
    Reminders reminders{settings};
    EyePopup eye;
    std::unique_ptr<BreakCard> card;   // the break showing now, if any
    Clock::time_point pausedUntil{};
};

} // namespace bb
