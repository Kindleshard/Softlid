using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;

namespace BlinkBuddy
{
    // All artwork is drawn in code, so there are no image resources to ship.
    static class IconArt
    {
        public static GraphicsPath RoundRect(RectangleF r, float radius)
        {
            var p = new GraphicsPath();
            float d = Math.Min(radius * 2, Math.Min(r.Width, r.Height));
            if (d <= 0) { p.AddRectangle(r); return p; }
            p.AddArc(r.X, r.Y, d, d, 180, 90);
            p.AddArc(r.Right - d, r.Y, d, d, 270, 90);
            p.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
            p.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
            p.CloseFigure();
            return p;
        }

        // Almond-shaped eye inside r. open: 1 = open, 0 = shut. lookX/lookY: -1..1 move the iris. alpha fades all of it.
        public static void DrawEye(Graphics g, RectangleF r, float open, float lookX, float lookY, float stroke, float alpha = 1f)
        {
            Func<int, int> a = v => (int)(v * Math.Max(0f, Math.Min(1f, alpha)));
            g.SmoothingMode = SmoothingMode.AntiAlias;
            float hw = r.Width / 2, hh = r.Height / 2;
            float cx = r.X + hw, cy = r.Y + hh + hh * 0.15f;
            // A cubic Bezier bulges 3/4 of the way to its control points; the lids span -hh..+0.7hh around cy.
            float lowY = cy + hh * 0.93f;
            float upY = lowY + (cy - hh * 1.33f - lowY) * open;
            var left = new PointF(r.Left, cy);
            var right = new PointF(r.Right, cy);
            var u1 = new PointF(r.Left + hw * 0.3f, upY);
            var u2 = new PointF(r.Right - hw * 0.3f, upY);
            var l1 = new PointF(r.Left + hw * 0.3f, lowY);
            var l2 = new PointF(r.Right - hw * 0.3f, lowY);

            using (var lid = new GraphicsPath())
            {
                lid.AddBezier(left, u1, u2, right);
                lid.AddBezier(right, l2, l1, left);
                lid.CloseFigure();
                using (var halo = new Pen(Color.FromArgb(a(90), 0, 0, 0), stroke * 2.4f)) { halo.LineJoin = LineJoin.Round; g.DrawPath(halo, lid); }
                if (open > 0.03f)
                {
                    using (var white = new SolidBrush(Color.FromArgb(a(255), 246, 247, 250))) g.FillPath(white, lid);
                    GraphicsState state = g.Save();
                    g.SetClip(lid, CombineMode.Intersect);
                    float ir = hh * 0.8f, ix = cx + lookX * hw * 0.38f, iy = cy - hh * 0.12f + lookY * hh * 0.3f;
                    using (var b = new SolidBrush(Color.FromArgb(a(255), Theme.Accent))) g.FillEllipse(b, ix - ir, iy - ir, 2 * ir, 2 * ir);
                    float pr = ir * 0.5f;
                    using (var b = new SolidBrush(Color.FromArgb(a(255), 18, 22, 30))) g.FillEllipse(b, ix - pr, iy - pr, 2 * pr, 2 * pr);
                    float hr = ir * 0.2f, hx = ix - ir * 0.38f, hy = iy - ir * 0.38f;
                    using (var b = new SolidBrush(Color.FromArgb(a(255), Color.White))) g.FillEllipse(b, hx - hr, hy - hr, 2 * hr, 2 * hr);
                    g.Restore(state);
                }
                using (var pen = new Pen(Color.FromArgb(a(255), Color.White), stroke)) { pen.LineJoin = LineJoin.Round; g.DrawPath(pen, lid); }
            }

            if (open < 0.3f)
            {
                int lash = a((int)(255 * (0.3f - open) / 0.3f));
                using (var pen = new Pen(Color.FromArgb(lash, Color.White), stroke))
                {
                    pen.StartCap = pen.EndCap = LineCap.Round;
                    foreach (float t in new[] { 0.28f, 0.5f, 0.72f })
                    {
                        PointF p = Bezier(left, l1, l2, right, t);
                        g.DrawLine(pen, p.X, p.Y + stroke, p.X + (p.X - cx) * 0.35f, p.Y + hh * 0.45f);
                    }
                }
            }
        }

        static PointF Bezier(PointF a, PointF b, PointF c, PointF d, float t)
        {
            float u = 1 - t, w0 = u * u * u, w1 = 3 * u * u * t, w2 = 3 * u * t * t, w3 = t * t * t;
            return new PointF(w0 * a.X + w1 * b.X + w2 * c.X + w3 * d.X, w0 * a.Y + w1 * b.Y + w2 * c.Y + w3 * d.Y);
        }

        // The on-screen blink cue: a dark pill so it reads on any background, with the eye inside.
        public static void DrawPopup(Graphics g, int w, int h, float open)
        {
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(Color.Transparent);
            var pill = new RectangleF(1, 1, w - 2, h - 2);
            using (GraphicsPath path = RoundRect(pill, pill.Height / 2))
            using (var b = new SolidBrush(Color.FromArgb(205, 24, 26, 32)))
                g.FillPath(b, path);
            float ew = w * 0.62f, eh = ew * 0.58f;
            DrawEye(g, new RectangleF((w - ew) / 2, (h - eh) / 2, ew, eh), open, 0f, 0f, Math.Max(1.5f, w * 0.026f));
        }

        public static Bitmap AppIcon(int size)
        {
            var bmp = new Bitmap(size, size, PixelFormat.Format32bppArgb);
            using (Graphics g = Graphics.FromImage(bmp))
            {
                g.SmoothingMode = SmoothingMode.AntiAlias;
                var tile = new RectangleF(0.5f, 0.5f, size - 1, size - 1);
                using (GraphicsPath path = RoundRect(tile, size * 0.22f))
                using (var b = new LinearGradientBrush(tile, Color.FromArgb(58, 66, 82), Color.FromArgb(26, 28, 36), 90f))
                    g.FillPath(b, path);
                float ew = size * 0.8f, eh = ew * 0.6f;
                DrawEye(g, new RectangleF((size - ew) / 2, (size - eh) / 2, ew, eh), 1f, 0f, 0f, Math.Max(1f, size * 0.045f));
            }
            return bmp;
        }

        // Mini mode's symbol, the familiar "picture in picture": a window outline with a small filled window tucked
        // into its corner and an arrow pointing there - this window shrinks into a small one that floats on top.
        public static Bitmap MiniModeIcon(int size)
        {
            var bmp = new Bitmap(size, size, PixelFormat.Format32bppArgb);
            using (Graphics g = Graphics.FromImage(bmp))
            {
                g.SmoothingMode = SmoothingMode.AntiAlias;
                float s = size, stroke = Math.Max(1.2f, s * 0.075f);
                var frame = new RectangleF(s * 0.06f, s * 0.14f, s * 0.88f, s * 0.72f);
                using (var pen = new Pen(Theme.Fg, stroke))
                using (GraphicsPath p = RoundRect(frame, s * 0.12f))
                    g.DrawPath(pen, p);
                var small = new RectangleF(s * 0.5f, s * 0.5f, s * 0.34f, s * 0.26f);
                using (var b = new SolidBrush(Theme.Accent))
                using (GraphicsPath p = RoundRect(small, s * 0.06f))
                    g.FillPath(b, p);
                using (var pen = new Pen(Theme.Fg, stroke) { StartCap = LineCap.Round, EndCap = LineCap.Round, LineJoin = LineJoin.Round })
                {
                    float ax = s * 0.2f, ay = s * 0.28f, bx = s * 0.4f, by = s * 0.46f;
                    g.DrawLine(pen, ax, ay, bx, by);
                    g.DrawLines(pen, new[] { new PointF(bx - s * 0.15f, by), new PointF(bx, by), new PointF(bx, by - s * 0.15f) });
                }
            }
            return bmp;
        }

        // The mini mode icons. hover (0..1) animates each one's "awake" state; bright (0..1) is the screen brightness
        // and warm (0..1) the blue light filter, which the lamp and the laptop show.
        public const int MiniEye = 0, MiniLamp = 1, MiniSun = 2, MiniLaptop = 3;
        static readonly Color Light = Color.FromArgb(255, 222, 140), SunCore = Color.FromArgb(255, 205, 80), SunEdge = Color.FromArgb(255, 150, 40);

        public static void DrawMiniIcon(Graphics g, RectangleF box, int kind, float hover, float bright, float warm)
        {
            float s = box.Width, cx = box.X + s / 2, cy = box.Y + s / 2, stroke = Math.Max(1.2f, s * 0.055f);
            Color line = Color.FromArgb(225, 232, 238);
            if (kind == MiniLamp) Lamp(g, box, hover, bright, line, stroke);
            else if (kind == MiniSun) Sun(g, cx, cy, s, hover, line, stroke);
            else if (kind == MiniLaptop) Laptop(g, box, hover, bright, warm, line, stroke);
            else
            {
                // The eye: shut and nearly gone while left alone; it opens when you come near.
                float ew = s * 0.78f, eh = ew * 0.6f;
                DrawEye(g, new RectangleF(cx - ew / 2, cy - eh / 2, ew, eh), 0.06f + 0.94f * hover, 0f, 0f, stroke, 0.18f + 0.82f * hover);
            }
        }

        static Color Mix(Color a, Color b, float k)
        {
            k = Math.Max(0f, Math.Min(1f, k));
            return Color.FromArgb((int)(a.A + (b.A - a.A) * k), (int)(a.R + (b.R - a.R) * k), (int)(a.G + (b.G - a.G) * k), (int)(a.B + (b.B - a.B) * k));
        }

        static void Glow(Graphics g, float cx, float cy, float r, Color c, float strength)
        {
            if (strength <= 0.01f || r < 1) return;
            using (var path = new GraphicsPath())
            {
                path.AddEllipse(cx - r, cy - r, 2 * r, 2 * r);
                using (var b = new PathGradientBrush(path) { CenterColor = Color.FromArgb((int)(255 * Math.Min(1f, strength)), c), SurroundColors = new[] { Color.FromArgb(0, c) } })
                    g.FillPath(b, path);
            }
        }

        // A light bulb lit as bright as the screen: full light at full brightness, just its outline when dimmed right
        // down. Hovering makes it glow a little more and sends out short rays.
        static void Lamp(Graphics g, RectangleF box, float hover, float bright, Color line, float stroke)
        {
            float s = box.Width, cx = box.X + s / 2, by = box.Y + s * 0.4f, r = s * 0.26f;
            float lit = Math.Max(0f, Math.Min(1f, bright));
            Glow(g, cx, by, r * (2.1f + 0.4f * hover), Light, lit * (0.42f + 0.3f * hover));
            using (var bulb = new GraphicsPath())
            {
                // round top narrowing into the neck
                bulb.AddArc(cx - r, by - r, 2 * r, 2 * r, 140, 260);
                bulb.AddLine(cx + r * 0.42f, by + r * 1.18f, cx - r * 0.42f, by + r * 1.18f);
                bulb.CloseFigure();
                using (var b = new SolidBrush(Color.FromArgb((int)(240 * lit), Mix(Light, Color.White, 0.25f * hover))))
                    g.FillPath(b, bulb);
                using (var pen = new Pen(Mix(line, Color.FromArgb(255, 214, 120), lit * 0.8f), stroke) { LineJoin = LineJoin.Round })
                    g.DrawPath(pen, bulb);
            }
            using (var pen = new Pen(line, stroke) { StartCap = LineCap.Round, EndCap = LineCap.Round })
            {
                float y = by + r * 1.18f + stroke * 1.3f;
                g.DrawLine(pen, cx - r * 0.36f, y, cx + r * 0.36f, y);
                g.DrawLine(pen, cx - r * 0.24f, y + stroke * 1.6f, cx + r * 0.24f, y + stroke * 1.6f);
            }
            if (hover <= 0.02f || lit <= 0.02f) return;
            using (var pen = new Pen(Color.FromArgb((int)(220 * hover * lit), Light), stroke * 0.9f) { StartCap = LineCap.Round, EndCap = LineCap.Round })
                for (int i = 0; i < 5; i++)
                {
                    double a = Math.PI * (1.0 + 0.25 * i);   // from the left, over the top, to the right
                    float r0 = r * 1.35f, r1 = r * (1.35f + 0.45f * hover);
                    g.DrawLine(pen, cx + (float)Math.Cos(a) * r0, by + (float)Math.Sin(a) * r0, cx + (float)Math.Cos(a) * r1, by + (float)Math.Sin(a) * r1);
                }
        }

        // The sun: an outline at rest; hovering fills it with warm light, lengthens and turns its rays and makes it glow.
        static void Sun(Graphics g, float cx, float cy, float s, float hover, Color line, float stroke)
        {
            float r = s * 0.19f;
            Glow(g, cx, cy, r * 3.1f, SunEdge, 0.5f * hover);
            if (hover > 0.01f)
                using (var path = new GraphicsPath())
                {
                    path.AddEllipse(cx - r, cy - r, 2 * r, 2 * r);
                    using (var b = new PathGradientBrush(path) { CenterColor = Color.FromArgb((int)(255 * hover), Mix(SunCore, Color.White, 0.3f)), SurroundColors = new[] { Color.FromArgb((int)(255 * hover), SunEdge) } })
                        g.FillPath(b, path);
                }
            Color ink = Mix(line, SunCore, hover);
            using (var pen = new Pen(ink, stroke)) g.DrawEllipse(pen, cx - r, cy - r, 2 * r, 2 * r);
            using (var pen = new Pen(ink, stroke * (1 + 0.3f * hover)) { StartCap = LineCap.Round, EndCap = LineCap.Round })
                for (int i = 0; i < 8; i++)
                {
                    double a = Math.PI / 4 * i + hover * Math.PI / 8;
                    float r0 = r * 1.45f, r1 = r * (1.95f + 0.3f * hover);
                    g.DrawLine(pen, cx + (float)Math.Cos(a) * r0, cy + (float)Math.Sin(a) * r0, cx + (float)Math.Cos(a) * r1, cy + (float)Math.Sin(a) * r1);
                }
        }

        // A laptop whose screen shows the screen settings: as bright as Brightness, and tinted from a calm blue (no
        // filter) to warm yellow (the blue light filter at full). Hovering lifts the screen's light into a soft glow.
        static void Laptop(Graphics g, RectangleF box, float hover, float bright, float warm, Color line, float stroke)
        {
            float s = box.Width, x = box.X, y = box.Y;
            var screen = new RectangleF(x + s * 0.17f, y + s * 0.2f, s * 0.66f, s * 0.44f);
            float level = 0.3f + 0.7f * Math.Max(0f, Math.Min(1f, bright)) + 0.12f * hover;
            Color tint = Mix(Color.FromArgb(96, 156, 228), Color.FromArgb(255, 198, 92), warm);
            Color shown = Color.FromArgb(Math.Min(255, (int)(tint.R * level)), Math.Min(255, (int)(tint.G * level)), Math.Min(255, (int)(tint.B * level)));
            Glow(g, screen.X + screen.Width / 2, screen.Y + screen.Height / 2, s * 0.5f, tint, 0.35f * hover * level);
            using (GraphicsPath p = RoundRect(screen, s * 0.05f))
            {
                using (var b = new SolidBrush(shown)) g.FillPath(b, p);
                using (var pen = new Pen(line, stroke)) g.DrawPath(pen, p);
            }
            // a faint sheen across the screen
            using (var b = new SolidBrush(Color.FromArgb((int)(40 + 30 * hover), 255, 255, 255)))
                g.FillPolygon(b, new[] { new PointF(screen.X + screen.Width * 0.08f, screen.Y + stroke), new PointF(screen.X + screen.Width * 0.38f, screen.Y + stroke),
                    new PointF(screen.X + screen.Width * 0.18f, screen.Bottom - stroke), new PointF(screen.X + stroke * 0.5f, screen.Bottom - stroke) });
            using (var pen = new Pen(line, stroke) { StartCap = LineCap.Round, EndCap = LineCap.Round })
            {
                float by = screen.Bottom + s * 0.07f;
                g.DrawLine(pen, x + s * 0.07f, by, x + s * 0.93f, by);
                g.DrawLine(pen, screen.X + s * 0.02f, screen.Bottom + stroke * 0.5f, x + s * 0.12f, by - stroke * 0.5f);
                g.DrawLine(pen, screen.Right - s * 0.02f, screen.Bottom + stroke * 0.5f, x + s * 0.88f, by - stroke * 0.5f);
            }
        }

        // A tiny flat eye for small sizes, where the full eye's detail turns to mush: lids and a pupil, in dark.
        static void DrawEyeMark(Graphics g, RectangleF r, float stroke)
        {
            float cx = r.X + r.Width / 2, cy = r.Y + r.Height / 2;
            using (var lid = new GraphicsPath())
            {
                lid.AddBezier(r.Left, cy, r.Left + r.Width * 0.3f, r.Top - r.Height * 0.15f, r.Right - r.Width * 0.3f, r.Top - r.Height * 0.15f, r.Right, cy);
                lid.AddBezier(r.Right, cy, r.Right - r.Width * 0.3f, r.Bottom + r.Height * 0.15f, r.Left + r.Width * 0.3f, r.Bottom + r.Height * 0.15f, r.Left, cy);
                lid.CloseFigure();
                using (var pen = new Pen(Color.FromArgb(18, 22, 30), stroke)) g.DrawPath(pen, lid);
            }
            float pr = r.Height * 0.3f;
            using (var b = new SolidBrush(Color.FromArgb(18, 22, 30))) g.FillEllipse(b, cx - pr, cy - pr, 2 * pr, 2 * pr);
        }

        // The HICON lives for the app's lifetime, so it is never destroyed.
        public static Icon MakeIcon(int size)
        {
            using (Bitmap b = AppIcon(size)) return Icon.FromHandle(b.GetHicon());
        }

        // Multi-size .ico with PNG-compressed entries, used as the exe's file icon at build time.
        public static void WriteIco(string path)
        {
            int[] sizes = { 16, 20, 24, 32, 40, 48, 64, 128, 256 };
            var images = new List<byte[]>();
            foreach (int s in sizes)
                using (Bitmap b = AppIcon(s))
                using (var ms = new MemoryStream())
                {
                    b.Save(ms, ImageFormat.Png);
                    images.Add(ms.ToArray());
                }
            using (var w = new BinaryWriter(File.Create(path)))
            {
                w.Write((short)0); w.Write((short)1); w.Write((short)sizes.Length);
                int offset = 6 + 16 * sizes.Length;
                for (int i = 0; i < sizes.Length; i++)
                {
                    byte dim = (byte)(sizes[i] >= 256 ? 0 : sizes[i]);
                    w.Write(dim); w.Write(dim); w.Write((byte)0); w.Write((byte)0);
                    w.Write((short)1); w.Write((short)32);
                    w.Write(images[i].Length); w.Write(offset);
                    offset += images[i].Length;
                }
                foreach (byte[] img in images) w.Write(img);
            }
        }
    }
}
