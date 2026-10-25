#include "ui/DirectWindow.h"
#include "platform/DarkMode.h"
#include "ui/Theme.h"
#include <commctrl.h>
#include <shellscalingapi.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "shcore.lib")

namespace bb::ui {
namespace {

constexpr int EditBackground = Palette::Argb(32, 32, 32);

bool IsWithin(const Control* c, const Control* ancestor)
{
    for (; c; c = c->Parent()) if (c == ancestor) return true;
    return false;
}

// Controls that take the keyboard, in Tab order.
void CollectFocusable(Control* c, std::vector<Control*>& out)
{
    if (!c->Visible() || !c->Enabled()) return;
    if (c->focusable && c->VisibleRect().right > 0) out.push_back(c);
    if (auto box = dynamic_cast<Container*>(c))
        for (auto& child : box->Children()) CollectFocusable(child.get(), out);
}

COLORREF Gdi(int argb) { return RGB((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF); }

} // namespace

DirectWindow::DirectWindow() { root.window = this; }

DirectWindow::~DirectWindow()
{
    dying = true;
    root.Clear();
    if (hwnd) for (Control* c : timers) KillTimer(hwnd, reinterpret_cast<UINT_PTR>(c));
    ReleaseGraphics();
    if (editBrush) DeleteObject(editBrush);
}

void DirectWindow::Open(float w, float h, const POINT* at, const Options& opts)
{
    options = opts;
    POINT cursor;
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(at ? *at : cursor, MONITOR_DEFAULTTONEAREST);
    UINT dx = 96, dy = 96;
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dx, &dy);
    dpi = dx;

    DWORD style = WS_CLIPCHILDREN, exStyle = 0;
    if (options.popup) {
        style |= WS_POPUP;
        exStyle |= WS_EX_TOOLWINDOW;
    } else {
        style |= WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | (options.resizable ? WS_THICKFRAME : 0);
    }
    RECT frame{0, 0, static_cast<LONG>(std::lround(w * Scale())), static_cast<LONG>(std::lround(h * Scale()))};
    AdjustWindowRectExForDpi(&frame, style, FALSE, exStyle, dpi);
    int fw = frame.right - frame.left, fh = frame.bottom - frame.top;
    RECT bounds;
    if (at) {
        bounds = {at->x, at->y, at->x + fw, at->y + fh};
    } else {
        MONITORINFO mi{sizeof mi};
        GetMonitorInfoW(monitor, &mi);
        RECT a = mi.rcWork;
        int x = a.left + (a.right - a.left - fw) / 2, y = a.top + std::max(0L, (a.bottom - a.top - fh) / 2);
        bounds = {x, y, x + fw, y + fh};
    }
    Create(options.popup ? L"BlinkBuddy.DirectPopup" : L"BlinkBuddy.Direct", style, exStyle, bounds, nullptr, options.owner,
           options.popup ? CS_DROPSHADOW : 0);
    if (!hwnd) return;
    SetWindowTextW(hwnd, options.title);
    dpi = GetDpiForWindow(hwnd);
    if (options.popup) DarkMode::RoundCorners(hwnd);
    else DarkMode::UseForTitleBar(hwnd);
    if (options.icon) {
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(options.icon));
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(options.icon));
    }
    fixedWidth = static_cast<float>(fw);

    tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0,
                              hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (tooltip) {
        SetWindowTheme(tooltip, L"DarkMode_Explorer", nullptr);
        TTTOOLINFOW tool{sizeof tool};
        tool.uFlags = TTF_SUBCLASS;
        tool.hwnd = hwnd;
        tool.uId = 1;
        tool.lpszText = const_cast<wchar_t*>(L"");
        SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        SendMessageW(tooltip, TTM_SETMAXTIPWIDTH, 0, static_cast<LPARAM>(360 * Scale()));
    }
    Relayout();
}

void DirectWindow::Show(bool activate)
{
    if (!hwnd) return;
    ShowWindow(hwnd, activate ? SW_SHOW : SW_SHOWNOACTIVATE);
    if (activate) SetForegroundWindow(hwnd);
}

void DirectWindow::Close()
{
    if (hwnd) PostMessageW(hwnd, WM_CLOSE, 0, 0);
}

D2D1_SIZE_F DirectWindow::ClientSize() const
{
    RECT r{};
    if (hwnd) GetClientRect(hwnd, &r);
    return {r.right / Scale(), r.bottom / Scale()};
}

POINT DirectWindow::ToScreen(D2D1_POINT_2F p) const
{
    POINT pt{static_cast<LONG>(std::lround(p.x * Scale())), static_cast<LONG>(std::lround(p.y * Scale()))};
    ClientToScreen(hwnd, &pt);
    return pt;
}

RECT DirectWindow::ToPixels(const D2D1_RECT_F& r) const
{
    float s = Scale();
    return {static_cast<LONG>(std::floor(r.left * s)), static_cast<LONG>(std::floor(r.top * s)),
            static_cast<LONG>(std::ceil(r.right * s)), static_cast<LONG>(std::ceil(r.bottom * s))};
}

void DirectWindow::Invalidate(const D2D1_RECT_F& r)
{
    if (!hwnd || r.right <= r.left || r.bottom <= r.top) return;
    RECT px = ToPixels(r);
    InflateRect(&px, 1, 1);   // anti-aliased edges reach a little past
    InvalidateRect(hwnd, &px, FALSE);
}

void DirectWindow::InvalidateAll()
{
    if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
}

void DirectWindow::SetFocus(Control* control)
{
    if (control == focused) return;
    Control* old = focused;
    focused = control;
    if (old) old->FocusChanged();
    if (control) {
        if (GetFocus() != hwnd) ::SetFocus(hwnd);
        control->FocusChanged();
    }
}

void DirectWindow::Placed() { root.Placed(); }

void DirectWindow::Relayout()
{
    D2D1_SIZE_F size = ClientSize();
    root.SetBounds(0, 0, size.width, size.height);
    Layout();
    Placed();
    InvalidateAll();
}

void DirectWindow::StartTimer(Control* control, UINT milliseconds)
{
    if (!hwnd) return;
    timers.insert(control);
    SetTimer(hwnd, reinterpret_cast<UINT_PTR>(control), milliseconds, nullptr);
}

void DirectWindow::StopTimer(Control* control)
{
    if (timers.erase(control) && hwnd) KillTimer(hwnd, reinterpret_cast<UINT_PTR>(control));
}

void DirectWindow::Forget(Control* control, bool timersToo)
{
    if (dying) return;
    if (IsWithin(hover, control)) hover = nullptr;
    if (IsWithin(captured, control)) {
        captured = nullptr;
        ReleaseCapture();
    }
    if (IsWithin(focused, control)) focused = nullptr;
    if (timersToo) StopTimer(control);
}

HBRUSH DirectWindow::EditBrush()
{
    if (!editBrush) editBrush = CreateSolidBrush(Gdi(EditBackground));
    return editBrush;
}

D2D1_POINT_2F DirectWindow::ToDips(LPARAM l) const
{
    return {GET_X_LPARAM(l) / Scale(), GET_Y_LPARAM(l) / Scale()};
}

// A control's own coordinates for a window point.
static D2D1_POINT_2F Local(const Control* c, D2D1_POINT_2F p)
{
    D2D1_POINT_2F o = c->Origin();
    return {p.x - o.x, p.y - o.y};
}

void DirectWindow::Paint()
{
    PAINTSTRUCT ps;
    BeginPaint(hwnd, &ps);
    if (!target) {
        RECT r;
        GetClientRect(hwnd, &r);
        auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(),
                                                  static_cast<float>(dpi), static_cast<float>(dpi));
        auto hwndProps = D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(r.right, r.bottom), D2D1_PRESENT_OPTIONS_RETAIN_CONTENTS);
        gfx::D2D()->CreateHwndRenderTarget(props, hwndProps, target.GetAddressOf());
    }
    if (target) {
        target->BeginDraw();
        target->SetTransform(D2D1::Matrix3x2F::Identity());
        float s = Scale();
        D2D1_RECT_F clip{ps.rcPaint.left / s, ps.rcPaint.top / s, ps.rcPaint.right / s, ps.rcPaint.bottom / s};
        target->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
        target->Clear(gfx::Color(options.popup ? Palette::Argb(43, 43, 43) : Theme::Bg));
        Canvas canvas(target.Get());
        root.PaintTree(canvas, clip);
        if (options.popup) {
            D2D1_SIZE_F size = ClientSize();
            float px = 1 / s;   // one pixel
            target->DrawRectangle({px / 2, px / 2, size.width - px / 2, size.height - px / 2}, canvas.Brush(Palette::Argb(72, 72, 72)), px);
        }
        target->PopAxisAlignedClip();
        if (target->EndDraw() == D2DERR_RECREATE_TARGET) target.Reset();
    }
    EndPaint(hwnd, &ps);
}

void DirectWindow::MoveFocus(bool back)
{
    std::vector<Control*> list;
    CollectFocusable(&root, list);
    if (list.empty()) return;
    auto at = std::find(list.begin(), list.end(), focused);
    size_t i = at == list.end() ? (back ? list.size() - 1 : 0)
             : back ? (at - list.begin() + list.size() - 1) % list.size() : (at - list.begin() + 1) % list.size();
    SetFocus(list[i]);
}

void DirectWindow::SetHover(Control* control)
{
    if (control == hover) return;
    Control* old = hover;
    hover = control;
    if (old) {
        old->hover = false;
        old->MouseLeave();
    }
    if (control) {
        control->hover = true;
        control->MouseEnter();
    }
    UpdateTip();
}

void DirectWindow::UpdateTip()
{
    if (!tooltip) return;
    TTTOOLINFOW tool{sizeof tool};
    tool.hwnd = hwnd;
    tool.uId = 1;
    tool.rect = hover && !hover->tip.empty() ? ToPixels(hover->VisibleRect()) : RECT{};
    SendMessageW(tooltip, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&tool));
    tool.lpszText = const_cast<wchar_t*>(hover ? hover->tip.c_str() : L"");
    SendMessageW(tooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
}

void DirectWindow::ReleaseGraphics() { target.Reset(); }

LRESULT DirectWindow::OnMessage(UINT message, WPARAM w, LPARAM l)
{
    switch (message) {
    case WM_PAINT:
        Paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (target) target->Resize(D2D1::SizeU(LOWORD(l), HIWORD(l)));
        if (w != SIZE_MINIMIZED) Relayout();
        return 0;
    case WM_DPICHANGED: {
        dpi = HIWORD(w);
        if (target) target->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
        auto r = reinterpret_cast<const RECT*>(l);
        fixedWidth = static_cast<float>(r->right - r->left);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        Relayout();
        return 0;
    }
    case WM_GETMINMAXINFO:
        if (options.resizable && fixedWidth > 0) {
            auto info = reinterpret_cast<MINMAXINFO*>(l);
            RECT frame{0, 0, 0, static_cast<LONG>(options.minHeight * Scale())};
            AdjustWindowRectExForDpi(&frame, static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE)), FALSE, 0, dpi);
            info->ptMinTrackSize.x = info->ptMaxTrackSize.x = static_cast<LONG>(fixedWidth);
            info->ptMinTrackSize.y = frame.bottom - frame.top;
        }
        return 0;
    case WM_MOUSEMOVE: {
        if (!tracking) {
            TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hwnd, 0};
            tracking = TrackMouseEvent(&t) != FALSE;
        }
        D2D1_POINT_2F p = ToDips(l);
        if (captured) {
            captured->MouseMove(Local(captured, p), true);
            return 0;
        }
        SetHover(root.HitTest(p));
        if (hover && hover->Enabled()) hover->MouseMove(Local(hover, p), false);
        return 0;
    }
    case WM_MOUSELEAVE:
        tracking = false;
        if (!captured) SetHover(nullptr);
        return 0;
    case WM_LBUTTONDOWN: {
        D2D1_POINT_2F p = ToDips(l);
        Control* hit = root.HitTest(p);
        SetHover(hit);
        if (!hit || !hit->Enabled()) return 0;
        if (hit->focusable) SetFocus(hit);
        else if (GetFocus() != hwnd) ::SetFocus(hwnd);
        captured = hit;
        SetCapture(hwnd);
        hit->MouseDown(Local(hit, p));
        return 0;
    }
    case WM_LBUTTONUP: {
        Control* c = captured;
        captured = nullptr;
        if (GetCapture() == hwnd) ReleaseCapture();
        if (!c) return 0;
        D2D1_POINT_2F local = Local(c, ToDips(l));
        c->MouseUp(local);
        // (The control may have been removed by its MouseUp: only click it if it is still under the pointer.)
        if (Contains({0, 0, c->W(), c->H()}, local) && root.HitTest(ToDips(l)) == c) c->Click();
        SetHover(root.HitTest(ToDips(l)));
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (captured && reinterpret_cast<HWND>(l) != hwnd) {
            Control* c = captured;
            captured = nullptr;
            c->MouseUp({-1, -1});
        }
        return 0;
    case WM_MOUSEWHEEL: {
        POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        ScreenToClient(hwnd, &pt);
        for (Control* c = root.HitTest({pt.x / Scale(), pt.y / Scale()}); c; c = c->Parent())
            if (c->Enabled() && c->Wheel(GET_WHEEL_DELTA_WPARAM(w))) break;
        return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT) {
            Control* c = captured ? captured : hover;
            Cursor kind = c && c->Enabled() ? c->cursor : Cursor::Arrow;
            SetCursor(LoadCursorW(nullptr, kind == Cursor::Hand ? IDC_HAND : kind == Cursor::Cross ? IDC_CROSS : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_KEYDOWN:
        if (w == VK_TAB) {
            MoveFocus(GetKeyState(VK_SHIFT) < 0);
            return 0;
        }
        if (focused && focused->Enabled() && focused->Key(static_cast<UINT>(w))) return 0;
        Shortcut(static_cast<UINT>(w));
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        if (focused) focused->Invalidate();
        return 0;
    case WM_ACTIVATE:
        if (options.closeOnDeactivate && LOWORD(w) == WA_INACTIVE) Close();
        break;
    case WM_TIMER: {
        auto c = reinterpret_cast<Control*>(w);
        if (timers.count(c)) c->Timer();
        return 0;
    }
    case WM_COMMAND:
        // From a native child (a text box): the control it belongs to is kept with it.
        if (l) {
            auto c = reinterpret_cast<Control*>(GetWindowLongPtrW(reinterpret_cast<HWND>(l), GWLP_USERDATA));
            if (c) c->Notify(HIWORD(w));
        }
        return 0;
    case WM_CTLCOLOREDIT: {
        auto dc = reinterpret_cast<HDC>(w);
        SetTextColor(dc, Gdi(Theme::Fg));
        SetBkColor(dc, Gdi(EditBackground));
        return reinterpret_cast<LRESULT>(EditBrush());
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        for (Control* c : timers) KillTimer(hwnd, reinterpret_cast<UINT_PTR>(c));
        timers.clear();
        ReleaseGraphics();
        return 0;
    case WM_NCDESTROY: {
        LRESULT result = DefWindowProcW(hwnd, message, w, l);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        hwnd = nullptr;
        tooltip = nullptr;
        if (onClosed) {
            auto closed = onClosed;   // (a copy: it may delete this object, and with it onClosed)
            closed();
        }
        return result;
    }
    }
    return Window::OnMessage(message, w, l);
}

} // namespace bb::ui
