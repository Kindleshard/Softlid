#pragma once
#include <windows.h>

namespace bb {

// A window that handles its own messages: derive, override Handle, call Create. The class name is registered on
// first use. Destroying the C++ object destroys the window.
class Window {
public:
    Window() = default;
    virtual ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    HWND Handle() const { return hwnd; }
    bool Exists() const { return hwnd != nullptr; }

protected:
    bool Create(const wchar_t* className, DWORD style, DWORD exStyle, const RECT& bounds, HBRUSH background = nullptr);
    void Destroy();
    virtual LRESULT OnMessage(UINT message, WPARAM w, LPARAM l);

    HWND hwnd = nullptr;

private:
    static LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM w, LPARAM l);
};

// The usual styles of BlinkBuddy's pop-ups: on top, no taskbar button, never taking focus from what you type in.
constexpr DWORD PopupExStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;

// Shows a window on top without activating it.
void ShowOnTop(HWND window);

} // namespace bb
