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

DirectWindow::DirectWindow()
{
    root.window = this;
    Fonts::WindowOpened();
}

DirectWindow::~DirectWindow()
{
    dying = true;
    root.Clear();
    if (hwnd) for (Control* c : timers) KillTimer(hwnd, reinterpret_cast<UINT_PTR>(c));
    ReleaseGraphics();
    if (editBrush) DeleteObject(editBrush);
    Fonts::WindowClosed();   // (its controls are gone: fonts released meanwhile can go too)
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
        style |= WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | (options.dialog ? 0 : WS_MINIMIZEBOX) | (options.resizable ? WS_THICKFRAME : 0);
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
        // The drawing surface always matches the window (a size change can come while it doesn't exist yet).
        RECT r;
        GetClientRect(hwnd, &r);
        D2D1_SIZE_U drawn = target->GetPixelSize();
        if (drawn.width != static_cast<UINT32>(r.right) || drawn.height != static_cast<UINT32>(r.bottom)) {
            target->Resize(D2D1::SizeU(r.right, r.bottom));
            InvalidateRect(hwnd, nullptr, FALSE);   // all of it, now it is bigger
        }
        target->BeginDraw();
        target->SetTransform(D2D1::Matrix3x2F::Identity());
        float s = Scale();
        D2D1_RECT_F clip{ps.rcPaint.left / s, ps.rcPaint.top / s, ps.rcPaint.right / s, ps.rcPaint.bottom / s};
        target->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
        target->Clear(gfx::Color(options.background ? options.background : options.popup ? Palette::Argb(43, 43, 43) : Theme::Bg));
        Canvas canvas(target.Get());
        root.PaintTree(canvas, clip);
        if (options.popup && options.edge) {
            D2D1_SIZE_F size = ClientSize();
            float px = 1 / s;   // one pixel
            target->DrawRectangle({px / 2, px / 2, size.width - px / 2, size.height - px / 2}, canvas.Brush(Palette::Argb(72, 72, 72)), px);
        }
        target->PopAxisAlignedClip();
        if (target->EndDraw() == D2DERR_RECREATE_TARGET) {
            target.Reset();                          // (made again on the next paint)
            InvalidateRect(hwnd, nullptr, FALSE);   // and that frame is drawn again in full
        }
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
    case WM_PAINT: Paint(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: OnSize(w, l); return 0;
    case WM_DPICHANGED: OnDpiChanged(w, l); return 0;
    case WM_GETMINMAXINFO: OnMinMaxInfo(reinterpret_cast<MINMAXINFO*>(l)); return 0;
    case WM_MOUSEMOVE: OnMouseMove(ToDips(l)); return 0;
    case WM_MOUSELEAVE: OnMouseLeave(); return 0;
    case WM_LBUTTONDOWN: OnLeftDown(ToDips(l)); return 0;
    case WM_LBUTTONUP: OnLeftUp(ToDips(l)); return 0;
    case WM_CAPTURECHANGED: OnCaptureChanged(reinterpret_cast<HWND>(l)); return 0;
    case WM_MOUSEWHEEL: OnWheel(w, l); return 0;
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT) {
            SetPointer();
            return TRUE;
        }
        break;
    case WM_KEYDOWN: OnKeyDown(static_cast<UINT>(w)); return 0;
    case WM_SYSKEYDOWN:
        // Keys with Alt (and F10): a control that wants them (recording a shortcut) gets them; otherwise Windows' own.
        if (focused && focused->Enabled() && focused->Key(static_cast<UINT>(w))) return 0;
        break;
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
    case WM_COMMAND: OnCommand(w, l); return 0;
    case WM_CTLCOLOREDIT: return OnEditColors(reinterpret_cast<HDC>(w));
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: OnDestroy(); return 0;
    case WM_NCDESTROY: return OnFinalDestroy(w, l);
    }
    return Window::OnMessage(message, w, l);
}

// ---- Size and scale ----

void DirectWindow::OnSize(WPARAM w, LPARAM l)
{
    if (target) target->Resize(D2D1::SizeU(LOWORD(l), HIWORD(l)));
    if (w != SIZE_MINIMIZED) Relayout();
}

void DirectWindow::OnDpiChanged(WPARAM w, LPARAM l)
{
    dpi = HIWORD(w);
    if (target) target->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
    auto r = reinterpret_cast<const RECT*>(l);
    fixedWidth = static_cast<float>(r->right - r->left);
    SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
    Relayout();
}

// A resizable window keeps its width, and doesn't get shorter than options.minHeight.
void DirectWindow::OnMinMaxInfo(MINMAXINFO* info)
{
    if (!options.resizable || fixedWidth <= 0) return;
    RECT frame{0, 0, 0, static_cast<LONG>(options.minHeight * Scale())};
    AdjustWindowRectExForDpi(&frame, static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE)), FALSE, 0, dpi);
    info->ptMinTrackSize.x = info->ptMaxTrackSize.x = static_cast<LONG>(fixedWidth);
    info->ptMinTrackSize.y = frame.bottom - frame.top;
}

// ---- The mouse ----

void DirectWindow::OnMouseMove(D2D1_POINT_2F p)
{
    if (!tracking) {
        TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hwnd, 0};
        tracking = TrackMouseEvent(&t) != FALSE;
    }
    if (captured) {
        captured->MouseMove(Local(captured, p), true);
        return;
    }
    SetHover(root.HitTest(p));
    if (hover && hover->Enabled()) hover->MouseMove(Local(hover, p), false);
}

void DirectWindow::OnMouseLeave()
{
    tracking = false;
    if (!captured) SetHover(nullptr);
}

void DirectWindow::OnLeftDown(D2D1_POINT_2F p)
{
    Control* hit = root.HitTest(p);
    SetHover(hit);
    if (!hit || !hit->Enabled()) return;
    if (hit->focusable) SetFocus(hit);
    else if (GetFocus() != hwnd) ::SetFocus(hwnd);
    captured = hit;
    SetCapture(hwnd);
    hit->MouseDown(Local(hit, p));
}

void DirectWindow::OnLeftUp(D2D1_POINT_2F p)
{
    Control* c = captured;
    captured = nullptr;
    if (GetCapture() == hwnd) ReleaseCapture();
    if (!c) return;
    D2D1_POINT_2F local = Local(c, p);
    c->MouseUp(local);
    // (The control may have been removed by its MouseUp: only click it if it is still under the pointer.)
    if (Contains({0, 0, c->W(), c->H()}, local) && root.HitTest(p) == c) c->Click();
    SetHover(root.HitTest(p));
}

void DirectWindow::OnCaptureChanged(HWND newOwner)
{
    if (!captured || newOwner == hwnd) return;
    Control* c = captured;
    captured = nullptr;
    c->MouseUp({-1, -1});
}

// The wheel goes to the control under the pointer, or up its parents until one uses it (e.g. the page scrolls).
void DirectWindow::OnWheel(WPARAM w, LPARAM l)
{
    POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
    ScreenToClient(hwnd, &pt);
    for (Control* c = root.HitTest({pt.x / Scale(), pt.y / Scale()}); c; c = c->Parent())
        if (c->Enabled() && c->Wheel(GET_WHEEL_DELTA_WPARAM(w))) break;
}

void DirectWindow::SetPointer()
{
    Control* c = captured ? captured : hover;
    Cursor kind = c && c->Enabled() ? c->cursor : Cursor::Arrow;
    SetCursor(LoadCursorW(nullptr, kind == Cursor::Hand ? IDC_HAND : kind == Cursor::Cross ? IDC_CROSS : IDC_ARROW));
}

// ---- The keyboard ----

// Tab moves the focus; other keys go to the focused control, then (unused) to Shortcut.
void DirectWindow::OnKeyDown(UINT key)
{
    if (key == VK_TAB) {
        MoveFocus(GetKeyState(VK_SHIFT) < 0);
        return;
    }
    if (focused && focused->Enabled() && focused->Key(key)) return;
    Shortcut(key);
}

// ---- Native child windows (text boxes) ----

// From a native child: the control it belongs to is kept with it.
void DirectWindow::OnCommand(WPARAM w, LPARAM l)
{
    if (!l) return;
    auto c = reinterpret_cast<Control*>(GetWindowLongPtrW(reinterpret_cast<HWND>(l), GWLP_USERDATA));
    if (c) c->Notify(HIWORD(w));
}

LRESULT DirectWindow::OnEditColors(HDC dc)
{
    SetTextColor(dc, Gdi(Theme::Fg));
    SetBkColor(dc, Gdi(EditBackground));
    return reinterpret_cast<LRESULT>(EditBrush());
}

// ---- Closing ----

void DirectWindow::OnDestroy()
{
    for (Control* c : timers) KillTimer(hwnd, reinterpret_cast<UINT_PTR>(c));
    timers.clear();
    ReleaseGraphics();
}

// The last message: the object lets go of the window, then onClosed runs (it may free this object: see DeleteLater).
LRESULT DirectWindow::OnFinalDestroy(WPARAM w, LPARAM l)
{
    LRESULT result = DefWindowProcW(hwnd, WM_NCDESTROY, w, l);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    hwnd = nullptr;
    tooltip = nullptr;
    if (onClosed) {
        auto closed = onClosed;   // (a copy: it may delete this object, and with it onClosed)
        closed();
    }
    return result;
}

} // namespace bb::ui
