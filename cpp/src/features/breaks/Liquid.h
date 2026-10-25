#pragma once
#include "ui/PixelSurface.h"
#include <windows.h>
#include <cstdint>
#include <vector>

namespace bb {

// Draws liquid: a smooth field summed from blobs (and from the screen edges, which can hold a film of liquid), with
// the surface where the field crosses a threshold. Nearby liquid therefore flows together with a neck, and pulls
// apart the same way. Each pixel is shaded like water from the field's slope (deep body, bright fresnel rim, specular
// highlight, a faint bloom), and the first blob carries an iris and pupil, so the drop is an eye. Rendered at half
// resolution around the liquid only, scaled up smoothly into a layered window's pixels. Everything is in pixels.
class Liquid {
public:
    struct Blob {
        double x = 0, y = 0, r = 1, m = 0, sx = 1, sy = 1;   // centre, visible radius, strength 0..1, squash
        double vx = 0, vy = 0;                               // for the followers' springs
    };
    // A screen edge holding liquid: 0 left, 1 right, 2 top, 3 bottom; at: the point along the edge; a: the amount.
    struct Film {
        int edge = -1;
        double at = 0, a = 0, spread = 1, thick = 1;
    };

    explicit Liquid(int count) : blobs(count) {}

    std::vector<Blob> blobs;
    Film films[2];
    // The eye inside the first blob. With eyeShape 1 it becomes a real eye: an almond opening with a white, lids made
    // of the liquid around it, and open (0 shut .. 1 open) moving the lids.
    double lookX = 0, lookY = 0, iris = 1, eyeShape = 0, open = 1;
    double warm = 0;   // how warm the water looks (0 blue .. 1 amber), following the blue light filter

    void Render(SIZE screen, float alpha);   // the current state, into an image covering the area it reaches
    void Present(HWND window, POINT screenOrigin, BYTE opacity);   // the last render, over that area
    bool Empty() const { return area.right <= area.left; }

private:
    RECT Bounds(SIZE screen) const;
    void FillField(int fw, int fh, SIZE screen);
    void Upscale();
    double W(double blue, double amber) const { return blue + (amber - blue) * warm; }

    std::vector<float> field;
    std::vector<uint32_t> pixels;
    std::vector<int> cols, colW, rowMin, rowMax;
    PixelSurface surface;
    int bw = 0, bh = 0;
    RECT area{};   // on screen, what the image covers
};

} // namespace bb
