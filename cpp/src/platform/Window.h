#pragma once
#include <windows.h>
#include <memory>

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
    // owner: the window this one stays above (for pop-ups); classStyle: e.g. CS_DROPSHADOW (once per class name).
    bool Create(const wchar_t* className, DWORD style, DWORD exStyle, const RECT& bounds, HBRUSH background = nullptr,
                HWND owner = nullptr, UINT classStyle = 0);
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

// Hands an object to a window's thread with a posted message (from a worker thread, say): the receiver takes it with
// TakePosted and owns it from then on. If the post fails (the window is gone), the object is deleted here.
template <typename T> void PostOwned(HWND window, UINT message, WPARAM w, T value)
{
    auto* heap = new T(std::move(value));
    if (!PostMessageW(window, message, w, reinterpret_cast<LPARAM>(heap))) delete heap;
}

template <typename T> std::unique_ptr<T> TakePosted(LPARAM l) { return std::unique_ptr<T>(reinterpret_cast<T*>(l)); }

} // namespace bb
