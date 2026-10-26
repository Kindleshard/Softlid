#pragma once
#include "ui/Control.h"
#include <functional>
#include <string>
#include <vector>

namespace bb::ui {

// Text in a box: one line (cut short with "...") or wrapped. Texts are given in English and shown translated.
class Label : public Control {
public:
    explicit Label(std::wstring_view english = L"", IDWriteTextFormat* font = nullptr, int color = 0);
    void SetText(std::wstring_view english);       // translated
    void SetPlainText(std::wstring text);          // as it is (numbers, names)
    const std::wstring& Text() const { return text; }
    void SetColor(int argb);

    IDWriteTextFormat* font;
    HAlign align = HAlign::Left;
    bool wrap = false;
    bool top = false;   // text from the top (a note), not centred
    void Paint(Canvas& canvas) override;

private:
    std::wstring text;
    int color;
};

// One icon character (Segoe Fluent Icons), centred in its box.
class Icon : public Control {
public:
    Icon(wchar_t glyph, float points, int color) : glyph(glyph), points(points), color(color) {}
    void Set(wchar_t g, int argb);
    void Paint(Canvas& canvas) override { canvas.Glyph(glyph, Fonts::Glyphs(points), {0, 0, W(), H()}, color); }

private:
    wchar_t glyph;
    float points;
    int color;
};

// A Windows icon (e.g. a program's), drawn at its size in the middle of the box. It owns the icon.
class IconImage : public Control {
public:
    explicit IconImage(HICON icon) : icon(icon) {}
    ~IconImage() override;
    void Paint(Canvas& canvas) override;

private:
    HICON icon;
    gfx::ComPtr<ID2D1Bitmap> bitmap;
    ID2D1RenderTarget* madeFor = nullptr;
};

// An invisible area you can click (laid over a section's title, so a click anywhere on it folds the section).
class HotArea : public Control {
public:
    HotArea() { cursor = Cursor::Hand; }
    std::function<void()> onClick;
    void Click() override { if (onClick) onClick(); }
    void MouseEnter() override {}
    void MouseLeave() override {}
};

// A fully rounded button: a circle when it is square (an icon alone), a pill otherwise. Its text can be words, or a
// single icon character (Segoe Fluent Icons), or a picture drawn by `icon` beside the words.
class Button : public Control {
public:
    explicit Button(std::wstring_view english = L"");
    void SetText(std::wstring_view english);
    void SetGlyph(wchar_t glyph, float points = 10);
    void SetTint(int argb);   // 0: none; otherwise a dark wash of the colour with its edge and text (e.g. an update)

    std::function<void()> onClick;
    std::function<void(Canvas&, const D2D1_RECT_F&)> icon;   // a picture before the text
    float iconSize = 16;

    void Paint(Canvas& canvas) override;
    void MouseDown(D2D1_POINT_2F) override;
    void MouseUp(D2D1_POINT_2F) override;
    void Click() override;
    bool Key(UINT key) override;

private:
    std::wstring text;
    wchar_t glyph = 0;
    float glyphPoints = 10;
    int tint = 0;
    bool down = false;
};

// An on/off switch.
class Toggle : public Control {
public:
    explicit Toggle(bool on);
    bool On() const { return on; }
    void SetOn(bool value);   // (no onChange: that is for changes made with the mouse or keyboard)
    std::function<void(bool)> onChange;

    void Paint(Canvas& canvas) override;
    void Click() override;
    bool Key(UINT key) override;

private:
    bool on;
};

// A value on a track, dragged with the mouse or moved with the arrow keys (and the wheel, once clicked into).
class Slider : public Control {
public:
    Slider(int min, int max, int value);
    int Value() const { return value; }
    void SetValue(int v);                 // (no onChange)
    void SetRange(int newMin, int newMax);
    void SetMarker(int v);                // a value marked in green on the track (-1: none), e.g. the night strength
    void SetMuted(bool on);               // greyed but still movable (the blue light filter while an effect is on)
    std::function<void(int)> onChange;
    bool warm = false;                    // the blue light filter: a track from daylight to amber (icy below 0)
    bool shade = false;                   // brightness: a track from dark to light

    void Paint(Canvas& canvas) override;
    void MouseDown(D2D1_POINT_2F p) override;
    void MouseMove(D2D1_POINT_2F p, bool down) override;
    void MouseUp(D2D1_POINT_2F) override;
    bool Wheel(int delta) override;
    bool Key(UINT key) override;

private:
    void Change(int v);
    float Start() const;
    float End() const;
    float X(int v) const;
    int Tint() const;
    int min, max, value, marker = -1;
    bool muted = false, dragging = false;
};

// A few fixed steps on a line, o---o---o---o, with a label under each; the track is blue up to the chosen step.
// Selected can be -1 (none of them, e.g. while a custom curve is in use).
class StepSlider : public Control {
public:
    explicit StepSlider(std::vector<std::wstring> englishLabels);
    int Selected() const { return selected; }
    void SetSelected(int i);
    void SetSteps(std::vector<std::wstring> englishLabels);   // the choice is kept if it still exists
    std::function<void(int)> onChange;

    void Paint(Canvas& canvas) override;
    void MouseDown(D2D1_POINT_2F p) override;
    void MouseMove(D2D1_POINT_2F p, bool down) override;
    void MouseUp(D2D1_POINT_2F) override;
    bool Key(UINT key) override;

private:
    void Change(int i);
    float Pad() const;
    float X(int i) const;
    int Nearest(float x) const;
    std::vector<std::wstring> labels;
    int selected = -1;
    bool dragging = false;
};

// A thin line between parts of a window.
class Divider : public Control {
public:
    void Paint(Canvas& canvas) override { canvas.Fill({0, 0, W(), H()}, color); }
    int color = 0xFF3A3A3A;   // Theme::Line
};

// A thin bar filling up (0..1).
class ProgressLine : public Control {
public:
    void SetProgress(float p);
    void Paint(Canvas& canvas) override;

private:
    float progress = 0;
};

} // namespace bb::ui
