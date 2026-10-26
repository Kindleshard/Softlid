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

// ---- Row ----

// On a card: the card's part behind this row (its corners rounded at the block's first and last row), drawn within the
// row only, so it never covers a neighbour.
void Row::Paint(Canvas& canvas)
{
    if (!card) return;
    constexpr float Inset = 12, Radius = 8;
    ID2D1RenderTarget* t = canvas.Target();
    t->PushAxisAlignedClip({0, 0, W(), H()}, D2D1_ANTIALIAS_MODE_ALIASED);
    D2D1_RECT_F r{Inset, cardTop ? 0 : -Radius, W() - Inset, cardBottom ? H() : H() + Radius};
    canvas.FillRound(r, Radius, Theme::Card);
    t->PopAxisAlignedClip();
}

// ---- Stack ----

Row& Stack::Add(float height, std::function<bool()> shown)
{
    Row& r = page.Add<Row>();
    r.SetBounds(0, 0, page.W(), height);
    std::function<bool()> own = shown ? std::move(shown) : [] { return true; };
    if (section) own = [open = section, own] { return open() && own(); };
    if (blockShown) own = [inBlock = blockShown, own] { return inBlock() && own(); };
    r.shown = own;
    r.block = block;
    r.group = group >= 0 ? group : block;
    rows.push_back(&r);
    return r;
}

void Stack::BeginBlock(int id, std::function<bool()> shown)
{
    section = nullptr;
    block = id;
    blockShown = std::move(shown);
}

void Stack::EndBlock()
{
    section = nullptr;
    block = -1;
    blockShown = nullptr;
}

void Stack::Layout()
{
    // The rows in their places: those of no block as added; all the blocks' rows where the first block began, block
    // by block in the order set (blocks not in it after, as added).
    std::vector<int> ids = order;
    for (Row* r : rows)
        if (r->block >= 0 && std::find(ids.begin(), ids.end(), r->block) == ids.end()) ids.push_back(r->block);
    std::vector<Row*> laid;
    bool blocksDone = false;
    for (Row* r : rows) {
        if (r->block < 0) {
            laid.push_back(r);
        } else if (!blocksDone) {
            for (int id : ids)
                for (Row* b : rows)
                    if (b->block == id) laid.push_back(b);
            blocksDone = true;
        }
    }

    // Placed top to bottom; each card (a block, or a group of rows) gap apart from what is around it.
    std::vector<Row*> shown;
    for (Row* r : laid) {
        bool show = r->shown();
        r->SetVisible(show);
        if (show) shown.push_back(r);
    }
    float y = 0;
    int previous = -1;
    placed.clear();
    for (size_t i = 0; i < shown.size(); i++) {
        Row* r = shown[i];
        int card = r->group;
        if (card != previous && (card >= 0 || previous >= 0)) y += gap;   // into a card, out of one, or from one to the next
        r->card = cards && card >= 0;
        r->cardTop = card >= 0 && card != previous;
        r->cardBottom = card >= 0 && (i + 1 == shown.size() || shown[i + 1]->group != card);
        r->SetBounds(0, y, page.W(), r->H());
        if (r->block >= 0) {
            if (placed.empty() || placed.back().id != r->block) placed.push_back({r->block, y, y});
            placed.back().bottom = y + r->H();
        }
        y += r->H();
        previous = card;
    }
    if (previous >= 0) y += gap;
    page.SetContentHeight(y);
    page.Invalidate();   // the whole page: where rows ended before (now below them) belongs to the page
    if (DirectWindow* w = page.Window()) w->Placed();
}

} // namespace bb::ui
