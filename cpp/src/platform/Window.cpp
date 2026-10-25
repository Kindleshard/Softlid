#include "platform/Window.h"

namespace bb {

Window::~Window() { Destroy(); }

bool Window::Create(const wchar_t* className, DWORD style, DWORD exStyle, const RECT& bounds, HBRUSH background,
                    HWND owner, UINT classStyle)
{
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{sizeof wc};
    if (!GetClassInfoExW(instance, className, &wc)) {
        wc = {sizeof wc};
        wc.style = classStyle;
        wc.lpfnWndProc = Proc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = background;
        wc.lpszClassName = className;
        RegisterClassExW(&wc);
    }
    hwnd = CreateWindowExW(exStyle, className, L"BlinkBuddy", style, bounds.left, bounds.top,
                           bounds.right - bounds.left, bounds.bottom - bounds.top, owner, nullptr, instance, this);
    return hwnd != nullptr;
}

void Window::Destroy()
{
    if (!hwnd) return;
    HWND h = hwnd;
    hwnd = nullptr;
    SetWindowLongPtrW(h, GWLP_USERDATA, 0);   // no more messages to an object that is going away
    DestroyWindow(h);
}

LRESULT Window::OnMessage(UINT message, WPARAM w, LPARAM l) { return DefWindowProcW(hwnd, message, w, l); }

LRESULT CALLBACK Window::Proc(HWND window, UINT message, WPARAM w, LPARAM l)
{
    if (message == WM_NCCREATE) {
        auto self = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        self->hwnd = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto self = reinterpret_cast<Window*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    return self ? self->OnMessage(message, w, l) : DefWindowProcW(window, message, w, l);
}

void ShowOnTop(HWND window)
{
    SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

} // namespace bb
