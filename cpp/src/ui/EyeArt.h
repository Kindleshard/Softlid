#pragma once
#include <d2d1.h>

namespace bb::EyeArt {

// Softlid's almond-shaped eye inside box. open: 1 open, 0 shut (lashes show as it closes). lookX/lookY: -1..1
// move the iris. stroke: the outline's width. alpha fades all of it. The iris is the accent colour.
void Draw(ID2D1RenderTarget* target, const D2D1_RECT_F& box, float open, float lookX, float lookY, float stroke, float alpha = 1.0f);

// The on-screen blink cue: a dark pill (so it reads on any background) with the eye inside, filling w x h.
void DrawPopup(ID2D1RenderTarget* target, float w, float h, float open);

} // namespace bb::EyeArt
