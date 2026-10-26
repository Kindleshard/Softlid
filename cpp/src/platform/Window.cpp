#include "platform/Window.h"
#include <vector>

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

// ---- Deleting later ----

namespace bb {
namespace {

// A hidden window whose only job is to free what DeleteLater was given, when its message comes round.
class Reaper : public Window {
public:
    static constexpr UINT Message = WM_APP + 1;
    std::vector<std::pair<std::shared_ptr<void>, std::function<void()>>> waiting;

    Reaper() { Create(L"BlinkBuddy.Reaper", WS_POPUP, WS_EX_TOOLWINDOW, {0, 0, 0, 0}); }

    void Run(bool followUps = true)
    {
        auto now = std::move(waiting);   // (an `after` may hand over more: those wait for the next round)
        waiting.clear();
        for (auto& [object, after] : now) {
            object.reset();
            if (after && followUps) after();
        }
    }

protected:
    LRESULT OnMessage(UINT message, WPARAM w, LPARAM l) override
    {
        if (message == Message) {
            Run();
            return 0;
        }
        return Window::OnMessage(message, w, l);
    }
};

Reaper& TheReaper()
{
    static Reaper reaper;
    return reaper;
}

} // namespace

void DeleteLater(std::shared_ptr<void> object, std::function<void()> after)
{
    Reaper& r = TheReaper();
    r.waiting.emplace_back(std::move(object), std::move(after));
    if (r.waiting.size() == 1) PostMessageW(r.Handle(), Reaper::Message, 0, 0);
}

void DeleteLaterNow(bool followUps) { TheReaper().Run(followUps); }

} // namespace bb
