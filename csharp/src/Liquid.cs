using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

namespace BlinkBuddy
{
    // Draws liquid: a smooth field summed from blobs (and from the screen edges, which can hold a film of liquid),
    // with the surface where the field crosses a threshold. Nearby liquid therefore flows together with a neck, and
    // pulls apart the same way. Each pixel is shaded like water from the field's slope (deep body, bright fresnel rim,
    // specular highlight, a faint bloom), and the first blob carries an iris and pupil, so the drop is an eye.
    // Rendered at half resolution around the liquid only, scaled up smoothly into a layered window's pixels.
    class Liquid : IDisposable
    {
        public class Blob
        {
            public double X, Y, R = 1, M, SX = 1, SY = 1;   // centre, visible radius (px), strength 0..1, squash
            public double VX, VY;                             // for the followers' springs
        }

        // A screen edge holding liquid: 0 left, 1 right, 2 top, 3 bottom; At = the point along the edge; A = amount.
        public class Film
        {
            public int Edge = -1;
            public double At, A, Spread = 1, Thick = 1;
        }
        public readonly Film[] Films = { new Film(), new Film() };

        // The eye inside the first blob. With EyeShape 1 it becomes a real eye: an almond opening with a white,
        // lids made of the liquid around it, and Open (0 shut .. 1 open) moving the lids.
        public double LookX, LookY, Iris = 1, EyeShape, Open = 1;
        // How warm the water looks (0 blue .. 1 amber), following the blue light filter.
        public double Warm;
        double W(double blue, double amber) { return blue + (amber - blue) * Warm; }

        const double T = 0.3;              // the surface: field = T
        const double KernelScale = 1 / 0.575;   // kernel radius for a blob that looks r wide at strength 1
        const float Q = 0.5f;              // working resolution
        public readonly Blob[] Blobs;
        float[] field = new float[0];
        int[] pixels = new int[0], line = new int[0], cols = new int[0], colW = new int[0];
        int[] rowMin = new int[0], rowMax = new int[0];   // per working row, the span that is not empty
        readonly LayeredSurface surface = new LayeredSurface();
        int bw, bh;
        Rectangle area;                    // on screen, what the image covers

        public Liquid(int count)
        {
            Blobs = new Blob[count];
            for (int i = 0; i < count; i++) Blobs[i] = new Blob();
        }

        // The screen area the liquid (and its glow) can reach right now.
        public Rectangle Bounds(Size screen)
        {
            double l = double.MaxValue, t = double.MaxValue, r = double.MinValue, b = double.MinValue;
            foreach (Blob o in Blobs)
            {
                if (o.M < 0.01) continue;
                double k = o.R * KernelScale * 1.15;
                l = Math.Min(l, o.X - k * o.SX); r = Math.Max(r, o.X + k * o.SX);
                t = Math.Min(t, o.Y - k * o.SY); b = Math.Max(b, o.Y + k * o.SY);
            }
            foreach (Film f in Films)
            {
                if (f.Edge < 0 || f.A <= 0.01) continue;
                double depth = f.Thick * 1.2, len = f.Spread * 3.2, at = f.At;
                switch (f.Edge)
                {
                    case 0: l = Math.Min(l, 0); r = Math.Max(r, depth); t = Math.Min(t, at - len); b = Math.Max(b, at + len); break;
                    case 1: l = Math.Min(l, screen.Width - depth); r = Math.Max(r, screen.Width); t = Math.Min(t, at - len); b = Math.Max(b, at + len); break;
                    case 2: t = Math.Min(t, 0); b = Math.Max(b, depth); l = Math.Min(l, at - len); r = Math.Max(r, at + len); break;
                    default: t = Math.Min(t, screen.Height - depth); b = Math.Max(b, screen.Height); l = Math.Min(l, at - len); r = Math.Max(r, at + len); break;
                }
            }
            if (l > r) return Rectangle.Empty;
            var box = Rectangle.FromLTRB((int)Math.Floor(l) - 4, (int)Math.Floor(t) - 4, (int)Math.Ceiling(r) + 4, (int)Math.Ceiling(b) + 4);
            return Rectangle.Intersect(box, new Rectangle(Point.Empty, screen));
        }

        // The field on the working grid: the blobs' kernels, each only over the cells it reaches, plus the edge film
        // (its fall-off along the edge worked out once per row or column).
        void FillField(int fw, int fh, Size screen)
        {
            Array.Clear(field, 0, fw * fh);
            double x0 = area.X - 0.5 / Q, y0 = area.Y - 0.5 / Q, step = 1 / Q;
            foreach (Blob o in Blobs)
            {
                if (o.M < 0.001) continue;
                double K = o.R * KernelScale, ax = 1 / (K * o.SX), ay = 1 / (K * o.SY);
                int i0 = Math.Max(0, (int)Math.Floor((o.X - K * o.SX - x0) / step)), i1 = Math.Min(fw - 1, (int)Math.Ceiling((o.X + K * o.SX - x0) / step));
                int j0 = Math.Max(0, (int)Math.Floor((o.Y - K * o.SY - y0) / step)), j1 = Math.Min(fh - 1, (int)Math.Ceiling((o.Y + K * o.SY - y0) / step));
                for (int j = j0; j <= j1; j++)
                {
                    double dy = (y0 + j * step - o.Y) * ay, dy2 = dy * dy;
                    if (dy2 >= 1) continue;
                    int row = j * fw;
                    for (int i = i0; i <= i1; i++)
                    {
                        double dx = (x0 + i * step - o.X) * ax, d2 = dx * dx + dy2;
                        if (d2 >= 1) continue;
                        double u = 1 - d2;
                        field[row + i] += (float)(o.M * u * u * u);
                    }
                }
            }
            foreach (Film f in Films)
            {
                if (f.Edge < 0 || f.A <= 0.001) continue;
                bool vertical = f.Edge <= 1;
                int n = vertical ? fh : fw, m = vertical ? fw : fh;
                double s2 = 1 / (2 * f.Spread * f.Spread), th2 = 1 / (f.Thick * f.Thick);
                for (int a = 0; a < n; a++)
                {
                    double along = (vertical ? y0 : x0) + a * step - f.At, fall = f.A * Math.Exp(-along * along * s2);
                    if (fall < 0.001) continue;
                    for (int c = 0; c < m; c++)
                    {
                        double p = (vertical ? x0 : y0) + c * step;
                        double d = f.Edge == 0 ? p : f.Edge == 1 ? screen.Width - p : f.Edge == 2 ? p : screen.Height - p;
                        if (d >= f.Thick || d < -f.Thick) continue;
                        double u = 1 - d * d * th2;
                        field[vertical ? a * fw + c : c * fw + a] += (float)(fall * u * u * u);
                    }
                }
            }
        }

        // Renders the liquid for the current state into an image covering Bounds(screen).
        public void Render(Size screen, float alpha)
        {
            area = Bounds(screen);
            if (area.IsEmpty) return;
            bw = Math.Max(1, (int)Math.Ceiling(area.Width * Q));
            bh = Math.Max(1, (int)Math.Ceiling(area.Height * Q));
            int fw = bw + 2, fh = bh + 2;
            if (field.Length < fw * fh) field = new float[fw * fh];
            if (pixels.Length < bw * bh) pixels = new int[bw * bh];
            if (rowMin.Length < bh) { rowMin = new int[bh]; rowMax = new int[bh]; }
            for (int j = 0; j < bh; j++) { rowMin[j] = int.MaxValue; rowMax[j] = -1; }
            FillField(fw, fh, screen);

            Blob eye = Blobs[0];
            double rb = Math.Max(2, eye.R * Q), slope = rb * 1.8;
            double irisR = eye.R * 0.5 * Math.Min(eye.SX, eye.SY), pupilR = irisR * 0.46;
            double ix = eye.X + LookX * eye.R * 0.32 * eye.SX, iy = eye.Y + LookY * eye.R * 0.32 * eye.SY;
            // light from the top left, viewer straight on
            double lx = -0.45, ly = -0.65, lz = 0.61, hx = lx, hy = ly, hz = lz + 1, hl = Math.Sqrt(hx * hx + hy * hy + hz * hz);
            hx /= hl; hy /= hl; hz /= hl;
            // The water's colours, from clear blue to amber as Warm (the blue light filter) rises: rim and depths of
            // the body, the fresnel sheen and the bloom.
            double br0 = W(70, 255), br1 = W(14, 150), bg0 = W(175, 214), bg1 = W(72, 78), bb0 = W(245, 120), bb1 = W(140, 12);
            double fr = W(150, 255), fgc = W(210, 226), fb = W(255, 150), gwr = W(40, 235), gwg = W(130, 150), gwb = W(230, 40);
            for (int j = 0; j < bh; j++)
                for (int i = 0; i < bw; i++)
                {
                    int c = (j + 1) * fw + (i + 1);
                    double f = field[c];
                    if (f < 0.06) { pixels[j * bw + i] = 0; continue; }   // far from any surface (and its bloom)
                    double gx = (field[c + 1] - field[c - 1]) * 0.5, gy = (field[c + fw] - field[c - fw]) * 0.5;
                    double g = Math.Sqrt(gx * gx + gy * gy) + 1e-6;
                    // coverage from the distance to the surface, for a clean anti-aliased edge
                    double cover = Math.Max(0, Math.Min(1, (f - T) / g + 0.5));
                    // a faint bloom just outside the surface
                    double gl = Math.Max(0, (f - 0.06) / (T - 0.06)), glow = f < T ? gl * gl * 0.28 : 0;
                    if (cover <= 0 && glow <= 0.003) { pixels[j * bw + i] = 0; continue; }

                    double depth = Math.Max(0, Math.Min(1, (f - T) / 0.75));
                    // The surface's height rises steeply at the rim and levels off inside, like a drop on glass, so
                    // where blobs overlap inside the body there are no stray bumps (and no stray highlights).
                    double rise = f > T ? Math.Exp(-(f - T) / 0.18) / 0.18 : 1 / 0.18;
                    double nx = -gx * slope * rise * 0.18, ny = -gy * slope * rise * 0.18, nz = 1, nl = Math.Sqrt(nx * nx + ny * ny + nz * nz);
                    nx /= nl; ny /= nl; nz /= nl;
                    double diffuse = Math.Max(0, nx * lx + ny * ly + nz * lz);
                    double h1 = Math.Max(0, nx * hx + ny * hy + nz * hz), h2 = h1 * h1, h4 = h2 * h2, h8 = h4 * h4, h64 = h8 * h8;
                    h64 *= h64;
                    double spec = h64 * h4 * h2;   // ^70
                    double fresnel = (1 - nz) * (1 - nz);
                    // water: light and clear at the rim, deep blue where thick
                    double r = br0 + (br1 - br0) * depth, gr = bg0 + (bg1 - bg0) * depth, b = bb0 + (bb1 - bb0) * depth;
                    double shade = 0.55 + 0.5 * diffuse;
                    r *= shade; gr *= shade; b *= shade;

                    // the eye: an iris and pupil seen through the water
                    if (Iris > 0.01 && cover > 0)
                    {
                        double sx = area.X + (i + 0.5) / Q, sy = area.Y + (j + 0.5) / Q;
                        double di = Math.Sqrt((sx - ix) * (sx - ix) + (sy - iy) * (sy - iy));
                        double edgeW = 1.6 / Q;
                        // The opening between the lids: an almond whose upper lid comes down to meet the lower one
                        // a little below the middle, so a shut eye is a soft curved line.
                        double opening = 1, lash = 0;
                        if (EyeShape > 0.01)
                        {
                            double ax = (sx - eye.X) / (eye.R * 0.88 * eye.SX), inside = 0;
                            if (Math.Abs(ax) < 1)
                            {
                                double bow = 1 - ax * ax, hgt = eye.R * 0.64 * eye.SY;
                                double top = eye.Y - hgt * (-0.26 + 1.26 * Open) * bow, bottom = eye.Y + hgt * (0.26 + 0.5 * Open) * bow;
                                inside = Clamp01((sy - top) / edgeW + 0.5) * Clamp01((bottom - sy) / edgeW + 0.5);
                                double w = eye.R * 0.045 + edgeW * 0.6, taper = Math.Sqrt(bow);
                                lash = Math.Max(Clamp01(1 - Math.Abs(sy - top) / w) * taper, Clamp01(1 - Math.Abs(sy - bottom) / (w * 0.6)) * taper * 0.45);
                                // the white, faintly blue and shaded by the lid above it
                                double shadow = Clamp01((sy - top) / (hgt * 0.5));
                                double wr = 196 + 40 * shadow, wg = 214 + 30 * shadow, wb = 236 + 16 * shadow, k = inside * EyeShape * cover;
                                r += (wr - r) * k; gr += (wg - gr) * k; b += (wb - b) * k;
                            }
                            opening = 1 - EyeShape * (1 - inside);
                            lash *= EyeShape * cover;
                        }
                        double inIris = Math.Max(0, Math.Min(1, (irisR - di) / edgeW + 0.5)) * Iris * cover * opening;
                        if (inIris > 0)
                        {
                            // bright teal by the pupil, deepening outwards to a dark ring at the edge
                            double ring = Math.Min(1, di / irisR), outer = Smooth01((ring - 0.45) / 0.55), limbus = Smooth01((ring - 0.8) / 0.2);
                            double ir = W(40, 215) + (W(10, 120) - W(40, 215)) * outer, ig = W(175, 150) + (W(80, 62) - W(175, 150)) * outer, ib = W(225, 40) + (W(165, 8) - W(225, 40)) * outer;
                            ir += (W(6, 50) - ir) * limbus * 0.8; ig += (W(28, 22) - ig) * limbus * 0.8; ib += (W(70, 4) - ib) * limbus * 0.8;
                            double inPupil = Math.Max(0, Math.Min(1, (pupilR - di) / edgeW + 0.5));
                            ir += (4 - ir) * inPupil; ig += (8 - ig) * inPupil; ib += (18 - ib) * inPupil;
                            r += (ir - r) * inIris; gr += (ig - gr) * inIris; b += (ib - b) * inIris;
                            // a catchlight: the window reflected in the eye
                            double cx = sx - (ix - irisR * 0.32), cy = sy - (iy - irisR * 0.36), cr = irisR * 0.17;
                            double catchLight = Math.Max(0, Math.Min(1, (cr - Math.Sqrt(cx * cx + cy * cy)) / edgeW + 0.5)) * inIris;
                            r += 230 * catchLight; gr += 240 * catchLight; b += 250 * catchLight;
                        }
                        if (lash > 0) { double k = 1 - 0.88 * lash; r = r * k + W(4, 30) * lash; gr = gr * k + W(14, 15) * lash; b = b * k + W(34, 4) * lash; }
                    }
                    r += fresnel * fr + spec * 255; gr += fresnel * fgc + spec * 255; b += fresnel * fb + spec * 255;

                    double a = Math.Min(1, cover + glow * (1 - cover)) * alpha;
                    if (cover < 1)   // outside the surface, the bloom's colour
                    {
                        double k = glow * (1 - cover) / Math.Max(1e-4, cover + glow * (1 - cover));
                        r += (gwr - r) * k; gr += (gwg - gr) * k; b += (gwb - b) * k;
                    }
                    a *= 0.92;   // calm, never glaring
                    int A8 = (int)(255 * Math.Max(0, Math.Min(1, a)));
                    int R8 = (int)(Math.Min(255, r) * A8 / 255), G8 = (int)(Math.Min(255, gr) * A8 / 255), B8 = (int)(Math.Min(255, b) * A8 / 255);
                    pixels[j * bw + i] = (A8 << 24) | (R8 << 16) | (G8 << 8) | B8;
                    if (A8 > 0) { if (i < rowMin[j]) rowMin[j] = i; if (i > rowMax[j]) rowMax[j] = i; }
                }
            Upscale();
        }

        // Scales the working image up to screen size (bilinear, in fixed point) straight into the layered window's
        // pixels, which the window then shows as they are: no conversion and no scaling by GDI+.
        void Upscale()
        {
            int ow = area.Width, oh = area.Height;
            surface.Ensure(ow, oh);
            if (cols.Length < ow) { cols = new int[ow]; colW = new int[ow]; }
            if (line.Length < ow) line = new int[ow];
            for (int x = 0; x < ow; x++)
            {
                double s = Math.Max(0, Math.Min(bw - 1.001, (x + 0.5) * Q - 0.5));
                cols[x] = (int)s;
                colW[x] = (int)((s - (int)s) * 256);
            }
            for (int y = 0; y < oh; y++)
            {
                double s = Math.Max(0, Math.Min(bh - 1.001, (y + 0.5) * Q - 0.5));
                int j0 = (int)s, j1 = Math.Min(bh - 1, j0 + 1);
                int r0 = j0 * bw, r1 = j1 * bw, wy = (int)((s - (int)s) * 256);
                Array.Clear(line, 0, ow);
                int lo = Math.Min(rowMin[j0], rowMin[j1]), hi = Math.Max(rowMax[j0], rowMax[j1]);
                // output columns that read working columns lo-1 .. hi+1
                int x0 = hi < 0 ? ow : Math.Max(0, (int)((lo - 1 + 0.5) / Q) - 1), x1 = hi < 0 ? -1 : Math.Min(ow - 1, (int)((hi + 1 + 0.5) / Q) + 1);
                for (int x = x0; x <= x1; x++)
                {
                    int c = cols[x], c1 = Math.Min(bw - 1, c + 1), wx = colW[x];
                    int p00 = pixels[r0 + c], p01 = pixels[r0 + c1], p10 = pixels[r1 + c], p11 = pixels[r1 + c1];
                    if ((p00 | p01 | p10 | p11) == 0) { line[x] = 0; continue; }
                    line[x] = Mix(Mix(p00, p01, wx), Mix(p10, p11, wx), wy);
                }
                Marshal.Copy(line, 0, surface.Row(y), ow);
            }
        }

        // Blends two premultiplied pixels, w/256 of the way from a to b.
        static int Mix(int a, int b, int w)
        {
            if (a == b) return a;
            int v = 256 - w;
            int rb = (int)(((((uint)a & 0x00FF00FF) * (uint)v + ((uint)b & 0x00FF00FF) * (uint)w) >> 8) & 0x00FF00FF);
            int ag = (int)((((((uint)a >> 8) & 0x00FF00FF) * (uint)v + (((uint)b >> 8) & 0x00FF00FF) * (uint)w)) & 0xFF00FF00);
            return rb | ag;
        }

        static double Smooth01(double x) { x = Math.Max(0, Math.Min(1, x)); return x * x * (3 - 2 * x); }
        static double Clamp01(double x) { return x < 0 ? 0 : x > 1 ? 1 : x; }

        public Rectangle Area { get { return area; } }

        // Shows the last render in a layered window, placed over the area it covers (screenOrigin: where the screen
        // starts), blended over whatever is behind, at the given overall opacity.
        public void Present(IntPtr hwnd, Point screenOrigin, byte opacity)
        {
            if (area.IsEmpty) return;
            surface.Present(hwnd, new Point(screenOrigin.X + area.X, screenOrigin.Y + area.Y), area.Size, opacity);
        }

        public void Dispose()
        {
            surface.Dispose();
        }
    }
}
