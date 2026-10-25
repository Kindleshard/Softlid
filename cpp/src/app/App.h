#pragma once
#include "app/Tray.h"
#include "core/Settings.h"
#include <windows.h>
#include <chrono>

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
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w, LPARAM l);
    LRESULT Handle(UINT message, WPARAM w, LPARAM l);

    void OnTick();
    void ShowMenu();
    void ShowSettings();
    void Pause(int minutes);   // -1: until resumed
    bool Paused() const;
    void Exit();

    HINSTANCE instance;
    HANDLE showRequest;   // set by a second copy of BlinkBuddy
    HWND window = nullptr;
    HICON icon = nullptr;
    UINT taskbarCreated = 0;
    Tray tray;
    Settings settings;
    bool dirty = false;
    std::chrono::system_clock::time_point pausedUntil{};
};

} // namespace bb
