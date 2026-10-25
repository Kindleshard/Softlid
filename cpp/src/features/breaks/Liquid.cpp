#include "features/breaks/Liquid.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>

namespace bb {
namespace {

constexpr double T = 0.3;                    // the surface: field = T
constexpr double KernelScale = 1 / 0.575;    // kernel radius for a blob that looks r wide at strength 1
constexpr double Q = 0.5;                    // working resolution

double Clamp01(double x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
double Smooth01(double x) { x = Clamp01(x); return x * x * (3 - 2 * x); }

// Blends two premultiplied pixels, w/256 of the way from a to b.
uint32_t Mix(uint32_t a, uint32_t b, uint32_t w)
{
    if (a == b) return a;
    uint32_t v = 256 - w;
    uint32_t rb = (((a & 0x00FF00FF) * v + (b & 0x00FF00FF) * w) >> 8) & 0x00FF00FF;
    uint32_t ag = (((a >> 8) & 0x00FF00FF) * v + ((b >> 8) & 0x00FF00FF) * w) & 0xFF00FF00;
    return rb | ag;
}

} // namespace

// The screen area the liquid (and its glow) can reach right now.
RECT Liquid::Bounds(SIZE screen) const
{
    double l = 1e30, t = 1e30, r = -1e30, b = -1e30;
    for (const Blob& o : blobs) {
        if (o.m < 0.01) continue;
        double k = o.r * KernelScale * 1.15;
        l = std::min(l, o.x - k * o.sx);
        r = std::max(r, o.x + k * o.sx);
        t = std::min(t, o.y - k * o.sy);
        b = std::max(b, o.y + k * o.sy);
    }
    for (const Film& f : films) {
        if (f.edge < 0 || f.a <= 0.01) continue;
        double depth = f.thick * 1.2, len = f.spread * 3.2, at = f.at;
        switch (f.edge) {
        case 0: l = std::min(l, 0.0); r = std::max(r, depth); t = std::min(t, at - len); b = std::max(b, at + len); break;
        case 1: l = std::min(l, screen.cx - depth); r = std::max(r, static_cast<double>(screen.cx)); t = std::min(t, at - len); b = std::max(b, at + len); break;
        case 2: t = std::min(t, 0.0); b = std::max(b, depth); l = std::min(l, at - len); r = std::max(r, at + len); break;
        default: t = std::min(t, screen.cy - depth); b = std::max(b, static_cast<double>(screen.cy)); l = std::min(l, at - len); r = std::max(r, at + len); break;
        }
    }
    if (l > r) return {};
    RECT box{static_cast<LONG>(std::floor(l)) - 4, static_cast<LONG>(std::floor(t)) - 4, static_cast<LONG>(std::ceil(r)) + 4,
             static_cast<LONG>(std::ceil(b)) + 4};
    RECT all{0, 0, screen.cx, screen.cy}, out;
    return IntersectRect(&out, &box, &all) ? out : RECT{};
}

// The field on the working grid: the blobs' kernels, each only over the cells it reaches, plus the edge film (its
// fall-off along the edge worked out once per row or column).
void Liquid::FillField(int fw, int fh, SIZE screen)
{
    std::fill(field.begin(), field.begin() + static_cast<size_t>(fw) * fh, 0.0f);
    double x0 = area.left - 0.5 / Q, y0 = area.top - 0.5 / Q, step = 1 / Q;
    for (const Blob& o : blobs) {
        if (o.m < 0.001) continue;
        double K = o.r * KernelScale, ax = 1 / (K * o.sx), ay = 1 / (K * o.sy);
        int i0 = std::max(0, static_cast<int>(std::floor((o.x - K * o.sx - x0) / step)));
        int i1 = std::min(fw - 1, static_cast<int>(std::ceil((o.x + K * o.sx - x0) / step)));
        int j0 = std::max(0, static_cast<int>(std::floor((o.y - K * o.sy - y0) / step)));
        int j1 = std::min(fh - 1, static_cast<int>(std::ceil((o.y + K * o.sy - y0) / step)));
        for (int j = j0; j <= j1; j++) {
            double dy = (y0 + j * step - o.y) * ay, dy2 = dy * dy;
            if (dy2 >= 1) continue;
            float* row = &field[static_cast<size_t>(j) * fw];
            for (int i = i0; i <= i1; i++) {
                double dx = (x0 + i * step - o.x) * ax, d2 = dx * dx + dy2;
                if (d2 >= 1) continue;
                double u = 1 - d2;
                row[i] += static_cast<float>(o.m * u * u * u);
            }
        }
    }
    for (const Film& f : films) {
        if (f.edge < 0 || f.a <= 0.001) continue;
        bool vertical = f.edge <= 1;
        int n = vertical ? fh : fw, m = vertical ? fw : fh;
        double s2 = 1 / (2 * f.spread * f.spread), th2 = 1 / (f.thick * f.thick);
        for (int a = 0; a < n; a++) {
            double along = (vertical ? y0 : x0) + a * step - f.at, fall = f.a * std::exp(-along * along * s2);
            if (fall < 0.001) continue;
            for (int c = 0; c < m; c++) {
                double p = (vertical ? x0 : y0) + c * step;
                double d = f.edge == 0 ? p : f.edge == 1 ? screen.cx - p : f.edge == 2 ? p : screen.cy - p;
                if (d >= f.thick || d < -f.thick) continue;
                double u = 1 - d * d * th2;
                field[vertical ? static_cast<size_t>(a) * fw + c : static_cast<size_t>(c) * fw + a] += static_cast<float>(fall * u * u * u);
            }
        }
    }
}

void Liquid::Render(SIZE screen, float alpha)
{
    area = Bounds(screen);
    if (Empty()) return;
    bw = std::max(1, static_cast<int>(std::ceil((area.right - area.left) * Q)));
    bh = std::max(1, static_cast<int>(std::ceil((area.bottom - area.top) * Q)));
    int fw = bw + 2, fh = bh + 2;
    if (field.size() < static_cast<size_t>(fw) * fh) field.resize(static_cast<size_t>(fw) * fh);
    if (pixels.size() < static_cast<size_t>(bw) * bh) pixels.resize(static_cast<size_t>(bw) * bh);
    if (rowMin.size() < static_cast<size_t>(bh)) {
        rowMin.resize(bh);
        rowMax.resize(bh);
    }
    for (int j = 0; j < bh; j++) {
        rowMin[j] = INT_MAX;
        rowMax[j] = -1;
    }
    FillField(fw, fh, screen);

    const Blob& eye = blobs[0];
    double rb = std::max(2.0, eye.r * Q), slope = rb * 1.8;
    double irisR = eye.r * 0.5 * std::min(eye.sx, eye.sy), pupilR = irisR * 0.46;
    double ix = eye.x + lookX * eye.r * 0.32 * eye.sx, iy = eye.y + lookY * eye.r * 0.32 * eye.sy;
    // Light from the top left, viewer straight on.
    double lx = -0.45, ly = -0.65, lz = 0.61, hx = lx, hy = ly, hz = lz + 1, hl = std::sqrt(hx * hx + hy * hy + hz * hz);
    hx /= hl;
    hy /= hl;
    hz /= hl;
    // The water's colours, from clear blue to amber as warm (the blue light filter) rises: rim and depths of the
    // body, the fresnel sheen and the bloom.
    double br0 = W(70, 255), br1 = W(14, 150), bg0 = W(175, 214), bg1 = W(72, 78), bb0 = W(245, 120), bb1 = W(140, 12);
    double fr = W(150, 255), fgc = W(210, 226), fb = W(255, 150), gwr = W(40, 235), gwg = W(130, 150), gwb = W(230, 40);
    for (int j = 0; j < bh; j++)
        for (int i = 0; i < bw; i++) {
            size_t c = static_cast<size_t>(j + 1) * fw + (i + 1);
            uint32_t& out = pixels[static_cast<size_t>(j) * bw + i];
            double f = field[c];
            if (f < 0.06) {   // far from any surface (and its bloom)
                out = 0;
                continue;
            }
            double gx = (field[c + 1] - field[c - 1]) * 0.5, gy = (field[c + fw] - field[c - fw]) * 0.5;
            double g = std::sqrt(gx * gx + gy * gy) + 1e-6;
            double cover = Clamp01((f - T) / g + 0.5);   // from the distance to the surface: a clean anti-aliased edge
            double gl = std::max(0.0, (f - 0.06) / (T - 0.06)), glow = f < T ? gl * gl * 0.28 : 0;   // a faint bloom outside
            if (cover <= 0 && glow <= 0.003) {
                out = 0;
                continue;
            }
            double depth = Clamp01((f - T) / 0.75);
            // The surface's height rises steeply at the rim and levels off inside, like a drop on glass, so where blobs
            // overlap inside the body there are no stray bumps (and no stray highlights).
            double rise = f > T ? std::exp(-(f - T) / 0.18) / 0.18 : 1 / 0.18;
            double nx = -gx * slope * rise * 0.18, ny = -gy * slope * rise * 0.18, nz = 1, nl = std::sqrt(nx * nx + ny * ny + nz * nz);
            nx /= nl;
            ny /= nl;
            nz /= nl;
            double diffuse = std::max(0.0, nx * lx + ny * ly + nz * lz);
            double h1 = std::max(0.0, nx * hx + ny * hy + nz * hz), h2 = h1 * h1, h4 = h2 * h2, h8 = h4 * h4, h64 = h8 * h8;
            h64 *= h64;
            double spec = h64 * h4 * h2;   // ^70
            double fresnel = (1 - nz) * (1 - nz);
            // Water: light and clear at the rim, deep where thick.
            double r = br0 + (br1 - br0) * depth, gr = bg0 + (bg1 - bg0) * depth, b = bb0 + (bb1 - bb0) * depth;
            double shade = 0.55 + 0.5 * diffuse;
            r *= shade;
            gr *= shade;
            b *= shade;

            // The eye: an iris and pupil seen through the water.
            if (iris > 0.01 && cover > 0) {
                double sx = area.left + (i + 0.5) / Q, sy = area.top + (j + 0.5) / Q;
                double di = std::sqrt((sx - ix) * (sx - ix) + (sy - iy) * (sy - iy));
                double edgeW = 1.6 / Q;
                // The opening between the lids: an almond whose upper lid comes down to meet the lower one a little
                // below the middle, so a shut eye is a soft curved line.
                double opening = 1, lash = 0;
                if (eyeShape > 0.01) {
                    double ax = (sx - eye.x) / (eye.r * 0.88 * eye.sx), inside = 0;
                    if (std::abs(ax) < 1) {
                        double bow = 1 - ax * ax, hgt = eye.r * 0.64 * eye.sy;
                        double top = eye.y - hgt * (-0.26 + 1.26 * open) * bow, bottom = eye.y + hgt * (0.26 + 0.5 * open) * bow;
                        inside = Clamp01((sy - top) / edgeW + 0.5) * Clamp01((bottom - sy) / edgeW + 0.5);
                        double w = eye.r * 0.045 + edgeW * 0.6, taper = std::sqrt(bow);
                        lash = std::max(Clamp01(1 - std::abs(sy - top) / w) * taper, Clamp01(1 - std::abs(sy - bottom) / (w * 0.6)) * taper * 0.45);
                        // The white, faintly blue and shaded by the lid above it.
                        double shadow = Clamp01((sy - top) / (hgt * 0.5));
                        double wr = 196 + 40 * shadow, wg = 214 + 30 * shadow, wb = 236 + 16 * shadow, k = inside * eyeShape * cover;
                        r += (wr - r) * k;
                        gr += (wg - gr) * k;
                        b += (wb - b) * k;
                    }
                    opening = 1 - eyeShape * (1 - inside);
                    lash *= eyeShape * cover;
                }
                double inIris = Clamp01((irisR - di) / edgeW + 0.5) * iris * cover * opening;
                if (inIris > 0) {
                    // Bright teal by the pupil, deepening outwards to a dark ring at the edge.
                    double ring = std::min(1.0, di / irisR), outer = Smooth01((ring - 0.45) / 0.55), limbus = Smooth01((ring - 0.8) / 0.2);
                    double ir = W(40, 215) + (W(10, 120) - W(40, 215)) * outer, ig = W(175, 150) + (W(80, 62) - W(175, 150)) * outer,
                           ib = W(225, 40) + (W(165, 8) - W(225, 40)) * outer;
                    ir += (W(6, 50) - ir) * limbus * 0.8;
                    ig += (W(28, 22) - ig) * limbus * 0.8;
                    ib += (W(70, 4) - ib) * limbus * 0.8;
                    double inPupil = Clamp01((pupilR - di) / edgeW + 0.5);
                    ir += (4 - ir) * inPupil;
                    ig += (8 - ig) * inPupil;
                    ib += (18 - ib) * inPupil;
                    r += (ir - r) * inIris;
                    gr += (ig - gr) * inIris;
                    b += (ib - b) * inIris;
                    // A catchlight: the window reflected in the eye.
                    double cx = sx - (ix - irisR * 0.32), cy = sy - (iy - irisR * 0.36), cr = irisR * 0.17;
                    double catchLight = Clamp01((cr - std::sqrt(cx * cx + cy * cy)) / edgeW + 0.5) * inIris;
                    r += 230 * catchLight;
                    gr += 240 * catchLight;
                    b += 250 * catchLight;
                }
                if (lash > 0) {
                    double k = 1 - 0.88 * lash;
                    r = r * k + W(4, 30) * lash;
                    gr = gr * k + W(14, 15) * lash;
                    b = b * k + W(34, 4) * lash;
                }
            }
            r += fresnel * fr + spec * 255;
            gr += fresnel * fgc + spec * 255;
            b += fresnel * fb + spec * 255;

            double a = std::min(1.0, cover + glow * (1 - cover)) * alpha;
            if (cover < 1) {   // outside the surface, the bloom's colour
                double k = glow * (1 - cover) / std::max(1e-4, cover + glow * (1 - cover));
                r += (gwr - r) * k;
                gr += (gwg - gr) * k;
                b += (gwb - b) * k;
            }
            a *= 0.92;   // calm, never glaring
            auto A8 = static_cast<uint32_t>(255 * Clamp01(a));
            auto R8 = static_cast<uint32_t>(std::min(255.0, r) * A8 / 255), G8 = static_cast<uint32_t>(std::min(255.0, gr) * A8 / 255),
                 B8 = static_cast<uint32_t>(std::min(255.0, b) * A8 / 255);
            out = (A8 << 24) | (R8 << 16) | (G8 << 8) | B8;
            if (A8 > 0) {
                rowMin[j] = std::min(rowMin[j], i);
                rowMax[j] = std::max(rowMax[j], i);
            }
        }
    Upscale();
}

// Scales the working image up to screen size (bilinear, in fixed point) straight into the layered window's pixels.
void Liquid::Upscale()
{
    int ow = area.right - area.left, oh = area.bottom - area.top;
    surface.Ensure(ow, oh, true);
    if (cols.size() < static_cast<size_t>(ow)) {
        cols.resize(ow);
        colW.resize(ow);
    }
    for (int x = 0; x < ow; x++) {
        double s = std::clamp((x + 0.5) * Q - 0.5, 0.0, bw - 1.001);
        cols[x] = static_cast<int>(s);
        colW[x] = static_cast<int>((s - static_cast<int>(s)) * 256);
    }
    for (int y = 0; y < oh; y++) {
        double s = std::clamp((y + 0.5) * Q - 0.5, 0.0, bh - 1.001);
        int j0 = static_cast<int>(s), j1 = std::min(bh - 1, j0 + 1);
        size_t r0 = static_cast<size_t>(j0) * bw, r1 = static_cast<size_t>(j1) * bw;
        auto wy = static_cast<uint32_t>((s - static_cast<int>(s)) * 256);
        uint32_t* line = surface.Row(y);
        std::memset(line, 0, ow * sizeof(uint32_t));
        int lo = std::min(rowMin[j0], rowMin[j1]), hi = std::max(rowMax[j0], rowMax[j1]);
        // Only the output columns that read working columns lo-1 .. hi+1.
        int x0 = hi < 0 ? ow : std::max(0, static_cast<int>((lo - 1 + 0.5) / Q) - 1);
        int x1 = hi < 0 ? -1 : std::min(ow - 1, static_cast<int>((hi + 1 + 0.5) / Q) + 1);
        for (int x = x0; x <= x1; x++) {
            int c = cols[x], c1 = std::min(bw - 1, c + 1);
            auto wx = static_cast<uint32_t>(colW[x]);
            uint32_t p00 = pixels[r0 + c], p01 = pixels[r0 + c1], p10 = pixels[r1 + c], p11 = pixels[r1 + c1];
            if ((p00 | p01 | p10 | p11) == 0) continue;
            line[x] = Mix(Mix(p00, p01, wx), Mix(p10, p11, wx), wy);
        }
    }
}

void Liquid::Present(HWND window, POINT screenOrigin, BYTE opacity)
{
    if (Empty()) return;
    surface.Present(window, {screenOrigin.x + area.left, screenOrigin.y + area.top}, area.right - area.left, area.bottom - area.top, opacity);
}

} // namespace bb
