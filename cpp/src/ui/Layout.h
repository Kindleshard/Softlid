#pragma once
#include "ui/Control.h"
#include <functional>
#include <vector>

namespace bb::ui {

// A page that can be taller than the window: its content scrolls under a thin bar on the right, by dragging the bar
// or with the mouse wheel anywhere over the page. Children are placed in content coordinates.
class ScrollPage : public Container {
public:
    void SetContentHeight(float h);
    float ContentHeight() const { return content; }
    void ScrollBy(float dy) { ScrollTo(offset + dy); }
    void ScrollTo(float y);
    bool AtEnd() const { return offset >= content - H() - 0.5f; }

    D2D1_POINT_2F ScrollOffset() const override { return {0, offset}; }
    Control* HitTest(D2D1_POINT_2F p) override;
    bool Wheel(int delta) override;
    void MouseDown(D2D1_POINT_2F p) override;
    void MouseMove(D2D1_POINT_2F p, bool down) override;
    void MouseUp(D2D1_POINT_2F p) override;

protected:
    void PaintOver(Canvas& canvas) override;

private:
    D2D1_RECT_F Bar() const;     // where the bar runs
    D2D1_RECT_F Thumb() const;   // the part you drag
    float content = 0, offset = 0, grabY = -1, grabOffset = 0;
};

// One row of a page: a fixed height, shown or not (hidden rows take no space). A row of a block can sit on a card:
// the card is drawn behind it, its corners rounded where the block starts and ends.
class Row : public Container {
public:
    std::function<bool()> shown = [] { return true; };
    int block = -1, group = -1;                  // (the block it belongs to, and the card: see Stack)
    bool card = false, cardTop = false, cardBottom = false;
    void Paint(Canvas& canvas) override;
};

// A page as a stack of rows, top to bottom. Rows added while a section is open belong to it and fold away with it.
class Stack {
public:
    explicit Stack(ScrollPage& page) : page(page) {}

    Row& Add(float height, std::function<bool()> shown = nullptr);
    void Section(std::function<bool()> open) { section = std::move(open); }
    void EndSection() { section = nullptr; }
    void Layout();   // rows that show or hide move the ones below

    // Blocks: rows added between BeginBlock and EndBlock belong to one, shown while `shown` says so. All the blocks
    // are laid out together, where the first one began, in the order SetOrder gives (any not in it keep the order
    // they were added in, after those that are), gap apart; on cards, each block is drawn on a card of its own.
    void BeginBlock(int id, std::function<bool()> shown = nullptr);
    void EndBlock();
    void SetOrder(std::vector<int> ids) { order = std::move(ids); }
    float gap = 0;
    bool cards = false;
    // Cards: by default each block is a card of its own; rows can be grouped onto cards instead (Group, then rows added
    // go on that card until the next Group; -1 none).
    void Group(int id) { group = id; }
    int CurrentGroup() const { return group; }
    struct Placed { int id; float top, bottom; };   // a block shown, in content coordinates
    std::vector<Placed> Blocks() const { return placed; }

    ScrollPage& Page() { return page; }
    float Width() const { return page.W(); }

private:
    ScrollPage& page;
    std::vector<Row*> rows;
    std::function<bool()> section, blockShown;
    int block = -1, group = -1;
    std::vector<int> order;
    std::vector<Placed> placed;
};

} // namespace bb::ui
