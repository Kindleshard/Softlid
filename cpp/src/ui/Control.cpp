#include "ui/Control.h"
#include "ui/DirectWindow.h"
#include <algorithm>

namespace bb::ui {
namespace {

D2D1_RECT_F Intersect(const D2D1_RECT_F& a, const D2D1_RECT_F& b)
{
    return {std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right), std::min(a.bottom, b.bottom)};
}

bool Empty(const D2D1_RECT_F& r) { return r.right <= r.left || r.bottom <= r.top; }

} // namespace

Control::~Control()
{
    if (DirectWindow* w = Window()) w->Forget(this);
}

void Control::SetBounds(float x, float y, float w, float h)
{
    D2D1_RECT_F r{x, y, x + w, y + h};
    if (r.left == bounds.left && r.top == bounds.top && r.right == bounds.right && r.bottom == bounds.bottom) return;
    Invalidate();
    bounds = r;
    Invalidate();
}

void Control::SetVisible(bool on)
{
    if (on == visible) return;
    if (!on) Invalidate();
    visible = on;
    if (on) Invalidate();
    if (DirectWindow* w = Window()) {
        if (!on) w->Forget(this, false);
        Placed();
    }
}

bool Control::Enabled() const { return enabled && (!parent || parent->Enabled()); }

void Control::SetEnabled(bool on)
{
    if (on == enabled) return;
    enabled = on;
    EnabledChanged();
}

DirectWindow* Control::Window() const
{
    const Control* c = this;
    while (c->parent) c = c->parent;
    auto root = dynamic_cast<const Container*>(c);
    return root ? root->window : nullptr;
}

void Control::Invalidate()
{
    if (DirectWindow* w = Window()) w->Invalidate(VisibleRect());
}

bool Control::Focused() const
{
    DirectWindow* w = Window();
    return w && w->Focused() == this && GetFocus() == w->Handle();
}

void Control::Focus()
{
    if (DirectWindow* w = Window()) w->SetFocus(this);
}

D2D1_POINT_2F Control::Origin() const
{
    D2D1_POINT_2F at{bounds.left, bounds.top};
    for (const Container* p = parent; p; p = p->parent) {
        D2D1_POINT_2F scroll = p->ScrollOffset();
        at.x += p->bounds.left - scroll.x;
        at.y += p->bounds.top - scroll.y;
    }
    return at;
}

D2D1_RECT_F Control::VisibleRect() const
{
    // (hidden, or on a hidden page: nothing of it shows, so a change to it repaints nothing)
    for (const Control* c = this; c; c = c->Parent())
        if (!c->Visible()) return {};
    D2D1_POINT_2F o = Origin();
    D2D1_RECT_F r{o.x, o.y, o.x + W(), o.y + H()};
    for (const Container* p = parent; p; p = p->parent) {
        D2D1_POINT_2F po = p->Origin();
        r = Intersect(r, {po.x, po.y, po.x + p->W(), po.y + p->H()});
    }
    return Empty(r) ? D2D1_RECT_F{} : r;
}

void Control::StartTimer(UINT milliseconds)
{
    if (DirectWindow* w = Window()) w->StartTimer(this, milliseconds);
}

void Control::StopTimer()
{
    if (DirectWindow* w = Window()) w->StopTimer(this);
}

Control* Control::HitTest(D2D1_POINT_2F p) { return visible && Contains(bounds, p) ? this : nullptr; }

void Control::PaintTree(Canvas& canvas, const D2D1_RECT_F& clip)
{
    if (!visible || Empty(Intersect(bounds, clip))) return;
    ID2D1RenderTarget* t = canvas.Target();
    D2D1_MATRIX_3X2_F saved;
    t->GetTransform(&saved);
    t->SetTransform(D2D1::Matrix3x2F::Translation(bounds.left, bounds.top) * saved);
    t->PushAxisAlignedClip({0, 0, W(), H()}, D2D1_ANTIALIAS_MODE_ALIASED);
    Paint(canvas);
    t->PopAxisAlignedClip();
    t->SetTransform(saved);
}

// ---- Container ----

void Container::Adopt(std::unique_ptr<Control> child)
{
    child->parent = this;
    children.push_back(std::move(child));
    children.back()->Invalidate();
}

void Container::Clear()
{
    Invalidate();
    children.clear();
}

Control* Container::HitTest(D2D1_POINT_2F p)
{
    if (!Visible() || !Contains(Bounds(), p)) return nullptr;
    D2D1_POINT_2F scroll = ScrollOffset();
    D2D1_POINT_2F local{p.x - Bounds().left + scroll.x, p.y - Bounds().top + scroll.y};
    for (auto it = children.rbegin(); it != children.rend(); ++it)   // the last added is on top
        if (Control* hit = (*it)->HitTest(local)) return hit;
    return this;
}

void Container::PaintTree(Canvas& canvas, const D2D1_RECT_F& clip)
{
    D2D1_RECT_F own = Intersect(Bounds(), clip);
    if (!Visible() || Empty(own)) return;
    ID2D1RenderTarget* t = canvas.Target();
    D2D1_MATRIX_3X2_F saved;
    t->GetTransform(&saved);
    t->SetTransform(D2D1::Matrix3x2F::Translation(Bounds().left, Bounds().top) * saved);
    t->PushAxisAlignedClip({0, 0, W(), H()}, D2D1_ANTIALIAS_MODE_ALIASED);
    Paint(canvas);
    D2D1_POINT_2F scroll = ScrollOffset();
    D2D1_MATRIX_3X2_F here;
    t->GetTransform(&here);
    t->SetTransform(D2D1::Matrix3x2F::Translation(-scroll.x, -scroll.y) * here);
    D2D1_RECT_F inner{own.left - Bounds().left + scroll.x, own.top - Bounds().top + scroll.y,
                      own.right - Bounds().left + scroll.x, own.bottom - Bounds().top + scroll.y};
    for (auto& child : children) child->PaintTree(canvas, inner);
    t->SetTransform(here);
    PaintOver(canvas);
    t->PopAxisAlignedClip();
    t->SetTransform(saved);
}

void Container::Placed()
{
    for (auto& child : children) child->Placed();
}

void Container::EnabledChanged()
{
    Invalidate();
    for (auto& child : children) child->EnabledChanged();
}

} // namespace bb::ui
