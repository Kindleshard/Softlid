#include "ui/Layout.h"
#include "ui/DirectWindow.h"
#include "ui/Theme.h"
#include <algorithm>

namespace bb::ui {

// ---- ScrollPage ----

void ScrollPage::SetContentHeight(float h)
{
    content = h;
    ScrollTo(offset);
    Invalidate();
}

void ScrollPage::ScrollTo(float y)
{
    float to = std::max(0.0f, std::min(y, content - H()));
    if (to == offset) return;
    offset = to;
    Invalidate();   // drawn afresh: nothing is copied along, so no bit of a row is left where it no longer is
    if (DirectWindow* w = Window()) w->Placed();
}

D2D1_RECT_F ScrollPage::Bar() const
{
    float w = std::max(4.0f, W() / 80);
    return {W() - w - 2, 0, W() - 2, H()};
}

D2D1_RECT_F ScrollPage::Thumb() const
{
    D2D1_RECT_F bar = Bar();
    float total = std::max(1.0f, content), view = H(), bw = ui::Width(bar);
    float h = std::max(bw * 6, H() * view / total);
    float y = (H() - h) * offset / std::max(1.0f, total - view);
    return {bar.left, y, bar.right, y + h};
}

Control* ScrollPage::HitTest(D2D1_POINT_2F p)
{
    if (!Visible() || !Contains(Bounds(), p)) return nullptr;
    D2D1_POINT_2F local{p.x - Bounds().left, p.y - Bounds().top};
    if (content > H() && Contains(Bar(), local)) return this;   // the bar is on top of the rows
    return Container::HitTest(p);
}

bool ScrollPage::Wheel(int delta)
{
    if (content <= H()) return false;
    ScrollBy(-delta / 120.0f * 48);
    return true;
}

void ScrollPage::MouseDown(D2D1_POINT_2F p)
{
    if (content <= H() || !Contains(Bar(), p)) return;
    grabY = p.y;
    grabOffset = offset;
    if (!Contains(Thumb(), p)) {   // a click beside the thumb jumps there
        ScrollTo(p.y / H() * content - H() / 2);
        grabOffset = offset;
    }
    Invalidate();
}

void ScrollPage::MouseMove(D2D1_POINT_2F p, bool down)
{
    if (!down || grabY < 0) return;
    float track = H() - ui::Height(Thumb());
    ScrollTo(grabOffset + (p.y - grabY) * (content - H()) / std::max(1.0f, track));
}

void ScrollPage::MouseUp(D2D1_POINT_2F)
{
    grabY = -1;
    Invalidate();
}

void ScrollPage::PaintOver(Canvas& canvas)
{
    if (content <= H()) return;
    D2D1_RECT_F t = Thumb();
    canvas.FillRound(t, ui::Width(t) / 2, grabY >= 0 ? Theme::Dim : Theme::Track);
}

// ---- Stack ----

Row& Stack::Add(float height, std::function<bool()> shown)
{
    Row& r = page.Add<Row>();
    r.SetBounds(0, 0, page.W(), height);
    std::function<bool()> own = shown ? std::move(shown) : [] { return true; };
    if (section) r.shown = [open = section, own] { return open() && own(); };
    else r.shown = own;
    rows.push_back(&r);
    return r;
}

void Stack::Layout()
{
    float y = 0;
    for (Row* r : rows) {
        bool show = r->shown();
        r->SetVisible(show);
        if (!show) continue;
        r->SetBounds(0, y, page.W(), r->H());
        y += r->H();
    }
    page.SetContentHeight(y);
    page.Invalidate();   // the whole page: where rows ended before (now below them) belongs to the page
    if (DirectWindow* w = page.Window()) w->Placed();
}

} // namespace bb::ui
