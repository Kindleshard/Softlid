#pragma once
#include <d2d1.h>

namespace bb::MiniArt {

// Mini mode's artwork, drawn in code (in 96-dpi units on any render target).

// The mini icons. hover (0..1) animates each one's "awake" state; bright (0..1) is the screen's brightness and warm
// (0..1) the blue light filter, which the lamp and the laptop show.
enum Icon { Eye = 0, Lamp, Sun, Laptop, Count };
void DrawIcon(ID2D1RenderTarget* t, const D2D1_RECT_F& box, int kind, float hover, float bright, float warm);

// Mini mode's symbol, the familiar "picture in picture": a window outline with a small filled window tucked into its
// corner and an arrow pointing there - this window shrinks into a small one that floats on top.
void DrawSymbol(ID2D1RenderTarget* t, const D2D1_RECT_F& box);

} // namespace bb::MiniArt
