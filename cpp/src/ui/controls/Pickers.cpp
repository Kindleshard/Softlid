#include "ui/controls/Pickers.h"
#include "core/Lang.h"
#include "platform/MemJournal.h"
#include "ui/DirectWindow.h"
#include "ui/EyeArt.h"
#include "ui/Theme.h"
#include <commctrl.h>
#include <algorithm>
#include <cmath>

namespace bb::ui {
namespace {

constexpr wchar_t ChevronDown = L'\uE70D', ChevronUp = L'\uE70E', Minus = L'\uE738', Plus = L'\uE710';

std::vector<std::wstring> Translated(std::vector<std::wstring> english)
{
    for (auto& s : english) s = Lang::T(s);
    return english;
}

} // namespace

// ---- DropDown ----

DropDown::DropDown(std::vector<std::wstring> englishItems) : items(Translated(std::move(englishItems)))
{
    cursor = Cursor::Hand;
    focusable = true;
}

void DropDown::SetItems(std::vector<std::wstring> englishItems) { SetPlainItems(Translated(std::move(englishItems))); }

void DropDown::SetPlainItems(std::vector<std::wstring> list)
{
    items = std::move(list);
    Invalidate();
}

void DropDown::Paint(Canvas& c)
{
    D2D1_RECT_F r{0.5f, 0.5f, W() - 1, H() - 1};
    c.FillRound(r, Height(r) / 2, hover ? Palette::Argb(62, 62, 62) : Palette::Argb(52, 52, 52));
    c.OutlineRound(r, Height(r) / 2, Focused() ? Theme::Accent() : Theme::ButtonEdge);
    float pad = H() / 2, arrow = H();
    std::wstring text = selected >= 0 && selected < static_cast<int>(items.size()) ? items[selected] : L"";
    c.Text(text, Fonts::Body(), {pad, 0, W() - arrow, H()}, Enabled() ? Theme::Fg : Theme::Dim);
    c.Glyph(ChevronDown, Fonts::Glyphs(), {W() - arrow - 4, 0, W() - 4, H()}, Theme::Dim);
}

void DropDown::Click() { Open(); }

bool DropDown::Key(UINT key)
{
    if (key == VK_SPACE || key == VK_RETURN) Open();
    else if (key == VK_DOWN && selected < static_cast<int>(items.size()) - 1) Pick(selected + 1);
    else if (key == VK_UP && selected > 0) Pick(selected - 1);
    else return false;
    return true;
}

void DropDown::Open()
{
    DirectWindow* w = Window();
    if (!w || items.empty()) return;
    HMENU menu = CreatePopupMenu();
    for (size_t i = 0; i < items.size(); i++)
        AppendMenuW(menu, MF_STRING | (static_cast<int>(i) == selected ? MF_CHECKED : 0), i + 1, items[i].c_str());
    D2D1_POINT_2F o = Origin();
    POINT at = w->ToScreen({o.x, o.y + H() + 2});
    UINT chosen = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN, at.x, at.y, w->Handle(), nullptr);
    DestroyMenu(menu);
    if (chosen > 0) Pick(static_cast<int>(chosen) - 1);
}

// ---- Disclosure ----

Disclosure::Disclosure(bool open) : open(open)
{
    cursor = Cursor::Hand;
    focusable = true;
}

void Disclosure::Paint(Canvas& c)
{
    int ink = hover || Focused() ? Theme::Fg : Theme::Accent();
    c.Glyph(open ? ChevronUp : ChevronDown, Fonts::Glyphs(8), {0, 0, H(), H()}, ink);
    c.Text(Lang::T(open ? L"Fewer options" : L"More options"), Fonts::Small(), {H(), 0, W(), H()}, ink);
}

void Disclosure::Click()
{
    open = !open;
    Invalidate();
    if (onToggle) onToggle(open);
}

bool Disclosure::Key(UINT key)
{
    if (key != VK_SPACE && key != VK_RETURN) return false;
    Click();
    return true;
}

// ---- FoldButton ----

FoldButton::FoldButton(bool open) : open(open) { cursor = Cursor::Hand; }

void FoldButton::SetOpen(bool on)
{
    if (on == open) return;
    open = on;
    Invalidate();
}

void FoldButton::Paint(Canvas& c)
{
    if (hover) c.FillCircle(W() / 2, H() / 2, std::min(W(), H()) / 2 - 0.5f, Palette::Argb(52, 52, 54));
    c.Glyph(open ? ChevronUp : ChevronDown, Fonts::Glyphs(), {0, 0, W(), H()}, hover ? Theme::Fg : Theme::Dim);
}

void FoldButton::Click()
{
    if (onClick) onClick();
}

// ---- Stepper ----

Stepper::Stepper(std::wstring_view englishLabel, int min, int max, int step, int value, std::function<std::wstring(int)> format)
    : label(Lang::T(englishLabel)), min(min), max(max), step(step), value(value), format(std::move(format))
{
    cursor = Cursor::Hand;
}

void Stepper::SetValue(int v)
{
    v = std::clamp(v, min, max);
    if (v == value) return;
    value = v;
    Invalidate();
}

void Stepper::Paint(Canvas& c)
{
    c.Text(label, Fonts::Small(), {0, 0, W(), LabelHeight()}, Theme::Dim, HAlign::Center);
    D2D1_RECT_F r{0.5f, LabelHeight(), W() - 1, H() - 1};
    float side = Height(r);
    c.FillRound(r, side / 2, Palette::Argb(48, 48, 50));
    c.OutlineRound(r, side / 2, Palette::Argb(72, 72, 74));
    for (int k = -1; k <= 1; k += 2) {
        D2D1_RECT_F b = Rect(k < 0 ? r.left + 3 : r.right - side + 3, r.top + 3, side - 6, side - 6);
        bool can = k < 0 ? value > min : value < max;
        if (hoverSide == k && can) c.FillCircle((b.left + b.right) / 2, (b.top + b.bottom) / 2, Width(b) / 2, Palette::Argb(66, 66, 68));
        c.Glyph(k < 0 ? Minus : Plus, Fonts::Glyphs(), b, can ? Theme::Fg : Palette::Argb(90, 90, 90));
    }
    c.Text(Lang::T(format(value)), Fonts::Body(), {r.left + side, r.top, r.right - side, r.bottom}, Theme::Fg, HAlign::Center);
}

int Stepper::SideAt(D2D1_POINT_2F p) const
{
    if (p.y < LabelHeight()) return 0;
    float side = H() - LabelHeight();
    return p.x < side ? -1 : p.x > W() - side ? 1 : 0;
}

void Stepper::Bump(int side)
{
    int v = std::clamp(value + side * step, min, max);
    if (v == value) return;
    value = v;
    Invalidate();
    if (onChange) onChange(value);
}

void Stepper::MouseMove(D2D1_POINT_2F p, bool)
{
    int k = SideAt(p);
    if (k == hoverSide) return;
    hoverSide = k;
    Invalidate();
}

void Stepper::MouseLeave()
{
    hoverSide = 0;
    Invalidate();
}

void Stepper::MouseDown(D2D1_POINT_2F p)
{
    held = SideAt(p);
    if (held == 0) return;
    Bump(held);
    repeating = false;
    StartTimer(400);   // held down: after a moment, it keeps stepping
}

void Stepper::MouseUp(D2D1_POINT_2F)
{
    held = 0;
    StopTimer();
}

void Stepper::Timer()
{
    if (!repeating) {
        repeating = true;
        StartTimer(70);
    }
    Bump(held);
}

// ---- TextBox ----

TextBox::TextBox(int maxLength, bool upperCase) : maxLength(maxLength), upperCase(upperCase) { cursor = Cursor::Arrow; }

TextBox::~TextBox()
{
    if (edit) DestroyWindow(edit);
    if (font) DeleteObject(font);
}

std::wstring TextBox::Text() const
{
    if (!edit) return pending;
    std::wstring text(GetWindowTextLengthW(edit) + 1, L'\0');
    text.resize(GetWindowTextW(edit, text.data(), static_cast<int>(text.size())));
    return text;
}

void TextBox::SetText(const std::wstring& text)
{
    pending = text;
    if (!edit) return;
    updating = true;
    SetWindowTextW(edit, text.c_str());
    updating = false;
}

void TextBox::Paint(Canvas& c)
{
    D2D1_RECT_F r{0.5f, 0.5f, W() - 0.5f, H() - 0.5f};
    bool typing = edit && GetFocus() == edit;
    c.FillRound(r, 4, Palette::Argb(32, 32, 32));
    c.OutlineRound(r, 4, typing ? Theme::Accent() : Theme::ButtonEdge);
}

void TextBox::Placed()
{
    DirectWindow* w = Window();
    if (!w || !w->Handle()) return;
    if (!edit) {
        DWORD style = WS_CHILD | ES_AUTOHSCROLL | (upperCase ? ES_UPPERCASE : 0);
        MemJournal::Note(L"text box made");
        edit = CreateWindowExW(0, L"EDIT", pending.c_str(), style, 0, 0, 0, 0, w->Handle(), nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!edit) return;
        SetWindowLongPtrW(edit, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));   // (the window routes EN_CHANGE here)
        SetWindowSubclass(edit, EditProc, 1, reinterpret_cast<DWORD_PTR>(this));
        if (maxLength > 0) SendMessageW(edit, EM_SETLIMITTEXT, maxLength, 0);
        if (!cue.empty()) SendMessageW(edit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(cue.c_str()));
    }
    UINT dpi = GetDpiForWindow(w->Handle());
    if (dpi != fontDpi) {
        if (font) DeleteObject(font);
        font = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, Lang::Family());
        fontDpi = dpi;
        SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    // Shown only while all of it can be seen (a native window can't be cut off by the page's edge).
    bool shown = true;
    for (const Control* c = this; c; c = c->Parent()) shown = shown && c->Visible();
    D2D1_RECT_F seen = VisibleRect();
    D2D1_POINT_2F o = Origin();
    shown = shown && std::abs(Width(seen) - W()) < 0.5f && std::abs(Height(seen) - H()) < 0.5f && o.y >= seen.top - 0.5f;
    if (!shown) {
        ShowWindow(edit, SW_HIDE);
        return;
    }
    // The text's own height, centred in the frame, with room on the left and right.
    TEXTMETRICW tm{};
    HDC dc = GetDC(edit);
    HGDIOBJ old = SelectObject(dc, font);
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(edit, dc);
    RECT px = w->ToPixels(Rect(o.x, o.y, W(), H()));
    int pad = static_cast<int>(6 * w->Scale()), h = tm.tmHeight;
    SetWindowPos(edit, nullptr, px.left + pad, px.top + (px.bottom - px.top - h) / 2, px.right - px.left - 2 * pad, h,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void TextBox::Notify(WORD code)
{
    if (code == EN_SETFOCUS || code == EN_KILLFOCUS) Invalidate();   // the frame lights up while typing
    if (code == EN_KILLFOCUS && onLeave) onLeave();
    if (code == EN_CHANGE && !updating && onChange) onChange(Text());
}

void TextBox::MouseDown(D2D1_POINT_2F)
{
    if (edit) ::SetFocus(edit);   // a click on the frame's edge types here too
}

void TextBox::StartTyping()
{
    if (edit) ::SetFocus(edit);
}

LRESULT CALLBACK TextBox::EditProc(HWND hwnd, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data)
{
    auto box = reinterpret_cast<TextBox*>(data);
    if (message == WM_KEYDOWN && (w == VK_UP || w == VK_DOWN) && box->onKey) {
        box->onKey(static_cast<UINT>(w));
        return 0;
    }
    if (message == WM_KEYDOWN && (w == VK_RETURN || w == VK_ESCAPE || w == VK_TAB)) {
        if (w != VK_TAB && box->onKey) box->onKey(static_cast<UINT>(w));
        else SendMessageW(GetParent(hwnd), WM_KEYDOWN, w, l);   // Tab moves on, as everywhere else in the window
        return 0;
    }
    if (message == WM_CHAR && (w == L'\r' || w == L'\x1b' || w == L'\t')) return 0;   // no beep
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, EditProc, 1);
        box->edit = nullptr;   // (gone with its window)
    }
    return DefSubclassProc(hwnd, message, w, l);
}

// ---- CurveView ----

void UsageGraph::Add(double a, double b)
{
    first.push_back(a);
    second.push_back(b);
    if (first.size() > Points) first.erase(first.begin());
    if (second.size() > Points) second.erase(second.begin());
    Invalidate();
}

void UsageGraph::Paint(Canvas& c)
{
    D2D1_RECT_F r{0.5f, 0.5f, W() - 0.5f, H() - 0.5f};
    c.Fill(r, Palette::Argb(40, 40, 40));
    c.Target()->DrawRectangle(r, c.Brush(Theme::Line), 1);
    // The scale: the next of 5, 10, 25, 50, 100% above the highest value.
    double top = 0;
    for (double v : first) top = std::max(top, v);
    for (double v : second) top = std::max(top, v);
    double scale = 100;
    for (double step : {5.0, 10.0, 25.0, 50.0}) if (top <= step) { scale = step; break; }
    c.Line(r.left, r.top + Height(r) / 2, r.right, r.top + Height(r) / 2, Theme::Line, 1, false);   // half the scale
    c.Text(std::to_wstring(static_cast<int>(scale)) + L"%", Fonts::Small(), Rect(r.left + 4, r.top + 1, 60, 16), Theme::Dim, HAlign::Left, false, true);
    auto draw = [&](const std::vector<double>& values, int color) {
        if (values.size() < 2) return;
        gfx::ComPtr<ID2D1PathGeometry> path;
        gfx::ComPtr<ID2D1GeometrySink> sink;
        gfx::D2D()->CreatePathGeometry(path.GetAddressOf());
        path->Open(sink.GetAddressOf());
        float step = Width(r) / (Points - 1), x0 = r.right - step * (values.size() - 1);
        for (size_t i = 0; i < values.size(); i++) {
            D2D1_POINT_2F p{x0 + step * i, r.bottom - 1 - static_cast<float>(std::min(values[i], scale) / scale) * (Height(r) - 2)};
            if (i == 0) sink->BeginFigure(p, D2D1_FIGURE_BEGIN_HOLLOW);
            else sink->AddLine(p);
        }
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
        c.Target()->DrawGeometry(path.Get(), c.Brush(color), 1.5f);
    };
    draw(second, Theme::Good);
    draw(first, Theme::Accent());
}

void CurveView::Paint(Canvas& c)
{
    D2D1_RECT_F r{0.5f, 0.5f, W() - 0.5f, H() - 0.5f};
    c.Fill(r, Palette::Argb(40, 40, 40));
    c.Target()->DrawRectangle(r, c.Brush(Theme::Line), 1);
    gfx::ComPtr<ID2D1StrokeStyle> dash;
    D2D1_STROKE_STYLE_PROPERTIES props{};
    props.dashStyle = D2D1_DASH_STYLE_DASH;
    gfx::D2D()->CreateStrokeStyle(props, nullptr, 0, dash.GetAddressOf());
    c.Target()->DrawLine({r.left, r.bottom}, {r.right, r.top}, c.Brush(Theme::Track), 1, dash.Get());
    if (!curve) return;
    gfx::ComPtr<ID2D1PathGeometry> path;
    gfx::ComPtr<ID2D1GeometrySink> sink;
    gfx::D2D()->CreatePathGeometry(path.GetAddressOf());
    path->Open(sink.GetAddressOf());
    for (int i = 0; i <= 64; i++) {
        double x = i / 64.0;
        D2D1_POINT_2F p{r.left + static_cast<float>(x) * Width(r), r.bottom - static_cast<float>(curve(x)) * Height(r)};
        if (i == 0) sink->BeginFigure(p, D2D1_FIGURE_BEGIN_HOLLOW);
        else sink->AddLine(p);
    }
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    c.Target()->DrawGeometry(path.Get(), c.Brush(Theme::Accent()), std::max(1.5f, H() / 60));
}

// ---- FocusRing ----

void FocusRing::Paint(Canvas& c)
{
    float thick = H() * 0.06f, d = std::min(W(), H()) - thick - 2, cx = W() / 2, cy = H() / 2;
    int color = ink ? ink() : Theme::Accent();
    c.OutlineCircle(cx, cy, d / 2, Palette::Argb(52, 52, 56), thick);
    float p = std::clamp(progress(), 0.0f, 1.0f);
    if (p > 0) {
        // From the top, clockwise.
        float sweep = std::max(1.0f, 360 * p), r = d / 2;
        auto at = [&](float deg) { return D2D1_POINT_2F{cx + r * std::cos(deg * 3.14159265f / 180), cy + r * std::sin(deg * 3.14159265f / 180)}; };
        if (sweep >= 359.9f) c.OutlineCircle(cx, cy, r, color, thick);
        else {
            gfx::ComPtr<ID2D1PathGeometry> path;
            gfx::ComPtr<ID2D1GeometrySink> sink;
            gfx::D2D()->CreatePathGeometry(path.GetAddressOf());
            path->Open(sink.GetAddressOf());
            sink->BeginFigure(at(-90), D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddArc({at(-90 + sweep), {r, r}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, sweep > 180 ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL});
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            gfx::ComPtr<ID2D1StrokeStyle> round;
            D2D1_STROKE_STYLE_PROPERTIES props{};
            props.startCap = props.endCap = D2D1_CAP_STYLE_ROUND;
            gfx::D2D()->CreateStrokeStyle(props, nullptr, 0, round.GetAddressOf());
            c.Target()->DrawGeometry(path.Get(), c.Brush(color), thick, round.Get());
        }
    }
    IDWriteTextFormat* big = Fonts::Get(22, gfx::Weight::Light);
    if (timeText) c.Text(timeText(), big, {0, cy - H() * 0.24f, W(), cy + H() * 0.06f}, Theme::Fg, HAlign::Center);
    if (stateText) c.Text(stateText(), Fonts::Small(), {0, cy + H() * 0.07f, W(), cy + H() * 0.2f}, Theme::Dim, HAlign::Center);
    float dot = H() * 0.035f, gap = dot * 2.6f, y = cy + H() * 0.25f;
    int finished = done ? done() : 0;
    for (int i = 0; i < 4; i++) c.FillCircle(W() / 2 + (i - 1.5f) * gap, y, dot / 2, i < finished ? color : Palette::Argb(70, 70, 74));
}

// ---- EyeView ----

void EyeView::Set(float x, float y, float openness)
{
    lookX = x;
    lookY = y;
    open = openness;
    Invalidate();
}

void EyeView::Paint(Canvas& c)
{
    // Leaves room below the eye for the lashes drawn when it closes.
    float pad = W() * 0.06f;
    EyeArt::Draw(c.Target(), Rect(pad, H() * 0.1f, W() - 2 * pad, H() * 0.62f), open, lookX, lookY, std::max(1.5f, W() * 0.035f));
}

} // namespace bb::ui
