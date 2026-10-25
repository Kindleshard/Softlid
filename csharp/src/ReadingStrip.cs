using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace BlinkBuddy
{
    // Click-through, never-focused, always-on-top tinted window.
    class Shade : Form
    {
        public Shade(Rectangle bounds, Color color, double opacity)
        {
            FormBorderStyle = FormBorderStyle.None;
            ShowInTaskbar = false;
            StartPosition = FormStartPosition.Manual;
            Bounds = bounds;
            BackColor = color;
            Opacity = opacity;   // kept below 1 so the window stays layered (and so click-through)
        }

        protected override bool ShowWithoutActivation { get { return true; } }

        protected override CreateParams CreateParams
        {
            get
            {
                CreateParams cp = base.CreateParams;
                cp.ExStyle |= Native.WS_EX_LAYERED | Native.WS_EX_TRANSPARENT | Native.WS_EX_TOOLWINDOW | Native.WS_EX_NOACTIVATE | Native.WS_EX_TOPMOST;
                return cp;
            }
        }
    }


    // Dims the screen except a clear area: a horizontal or vertical band, a rounded rectangle or a circle. The clear
    // area follows the mouse, or stays put and moves with the keys.
    // One tinted sheet covers the whole monitor and never moves or resizes; the clear area is a hole cut out of it, so
    // moving it is a single change and the dim can't tear or leave gaps. A soft edge or an outline is drawn once into
    // a small per-pixel window that fills the hole and only moves. Bands can dim just one side.
    class ReadingStrip : IDisposable
    {
        Shade dim;
        Tile tile;
        IntPtr hook;
        Native.HookProc proc;   // held in a field so the GC can't collect it while Windows calls it
        Settings s;
        float scale;
        Rectangle screen;
        Point at;
        bool placed;
        string drawnFor, cutFor;
        DateTime lastNudge;
        double speed;

        public void Apply(Settings s, float scale)
        {
            this.s = s;
            this.scale = scale;
            if (!s.StripEnabled) { Stop(); return; }
            if (dim == null)
            {
                dim = new Shade(new Rectangle(0, 0, 1, 1), Color.Black, 0.5);
                tile = new Tile();
            }
            if (s.StripFollow && hook == IntPtr.Zero)
            {
                proc = OnMouse;
                hook = Native.SetWindowsHookEx(14, proc, Native.GetModuleHandle(null), 0); // WH_MOUSE_LL
            }
            else if (!s.StripFollow) Unhook();
            if (s.StripFollow || !placed) at = Cursor.Position;
            if (screen.IsEmpty || !s.StripOneMonitor || !placed) screen = Screen.FromPoint(at).Bounds;
            placed = true;
            cutFor = null;   // the look may have changed: cut the hole again
            Place();
        }

        IntPtr OnMouse(int code, IntPtr w, IntPtr l)
        {
            if (code >= 0 && (int)w == 0x200) // WM_MOUSEMOVE
                MoveTo(new Point(Marshal.ReadInt32(l), Marshal.ReadInt32(l, 4)));
            return Native.CallNextHookEx(IntPtr.Zero, code, w, l);
        }

        void MoveTo(Point p)
        {
            if (p == at) return;
            if (!s.StripOneMonitor && !screen.Contains(p)) screen = Screen.FromPoint(p).Bounds;
            at = p;
            Place();
        }

        // The movement keys: a few pixels a press, speeding up while the key is held (how much: StripAccel).
        public void Nudge(int dx, int dy)
        {
            if (dim == null || s == null || !s.StripEnabled) return;
            DateTime now = DateTime.Now;
            double step = 3 * scale;
            speed = (now - lastNudge).TotalMilliseconds < 220 && s.StripAccel > 0
                ? Math.Min(90 * scale, Math.Max(step, speed) * (1 + s.StripAccel / 100.0 * 0.45))
                : step;
            lastNudge = now;
            at = new Point(
                Math.Max(screen.Left, Math.Min(screen.Right - 1, at.X + (int)Math.Round(dx * speed))),
                Math.Max(screen.Top, Math.Min(screen.Bottom - 1, at.Y + (int)Math.Round(dy * speed))));
            Place();
        }

        void Place()
        {
            Rectangle sc = screen;
            int shape = s.StripShape, edge = s.StripEdge, h = (int)(s.StripHeight * scale), w = (int)(s.StripWidth * scale);
            int feather = edge == 1 ? Math.Max(1, (int)(s.StripFeather * scale)) : 0;
            int border = edge == 2 ? Math.Max(1, (int)Math.Round(s.StripBorder * scale)) : 0;
            int pad = edge == 1 ? feather : edge == 2 ? border + 2 : 0;   // the edge sits just outside the clear area
            int alpha = (int)Math.Round(s.StripDim * 2.55), cover = shape <= 1 ? s.StripCover : 0;
            Color tint = Theme.Rgb(s.StripColor), line = s.StripLine == 0 ? Theme.Accent : Theme.Rgb(s.StripLine);
            int cx = Math.Max(sc.Left, Math.Min(sc.Right - 1, at.X)), cy = Math.Max(sc.Top, Math.Min(sc.Bottom - 1, at.Y));
            if (shape == 3) w = h;
            // the clear area with its edge round it
            Rectangle t = shape == 0 ? new Rectangle(sc.Left, cy - h / 2 - pad, sc.Width, h + 2 * pad)
                : shape == 1 ? new Rectangle(cx - w / 2 - pad, sc.Top, w + 2 * pad, sc.Height)
                : new Rectangle(cx - w / 2 - pad, cy - h / 2 - pad, w + 2 * pad, h + 2 * pad);

            // The edge first, so the hole never opens where it isn't yet.
            if (edge == 0) tile.Hide();
            else
            {
                string key = shape + "," + t.Size + "," + pad + "," + feather + "," + border + "," + alpha + "," + tint.ToArgb() + "," + cover + "," + line.ToArgb();
                if (key != drawnFor)
                {
                    drawnFor = key;
                    tile.Draw(t.Size, shape, pad, feather, alpha, tint, border, cover, line);
                    GC.Collect();
                    Native.TrimWorkingSet();
                }
                tile.Place(t, sc);
            }

            // The sheet: the whole monitor (or, for a band dimming one side, just that side), minus the hole. Without an
            // edge the hole has the clear area's own shape; with one it is the edge window's rectangle.
            if (dim.Bounds != sc || dim.BackColor != tint || Math.Abs(dim.Opacity - Math.Max(0.01, alpha / 255.0)) > 0.002)
            {
                Native.ClearRegion(dim.Handle);   // repaint all of it in the new colour, not just what shows now
                dim.BackColor = tint;
                dim.Opacity = Math.Max(0.01, alpha / 255.0);
                Native.PlaceTopmost(dim.Handle, sc);
                dim.Refresh();
                cutFor = null;
            }
            Rectangle keep = new Rectangle(0, 0, sc.Width, sc.Height);
            if (shape == 0 && cover == 1) keep = new Rectangle(0, 0, sc.Width, t.Top - sc.Top);
            if (shape == 0 && cover == 2) keep = new Rectangle(0, t.Bottom - sc.Top, sc.Width, sc.Bottom - t.Bottom);
            if (shape == 1 && cover == 1) keep = new Rectangle(0, 0, t.Left - sc.Left, sc.Height);
            if (shape == 1 && cover == 2) keep = new Rectangle(t.Right - sc.Left, 0, sc.Right - t.Right, sc.Height);
            Rectangle hole = new Rectangle(t.X - sc.X, t.Y - sc.Y, t.Width, t.Height);
            int kind = edge != 0 ? 0 : shape == 3 ? 2 : shape == 2 ? 1 : 0;
            int radius = (int)Corner(w / 2.0, h / 2.0);
            string cut = keep + "," + hole + "," + kind + "," + radius;
            if (cut != cutFor)
            {
                cutFor = cut;
                Native.CutHole(dim.Handle, keep, hole, kind, radius);
            }
            if (!dim.Visible) dim.Show();
        }

        // The rounded rectangle's corner radius, for half-sizes hx and hy.
        static double Corner(double hx, double hy) { return Math.Min(Math.Min(hx, hy), 6 + Math.Min(hx, hy) * 0.08); }

        void Unhook()
        {
            if (hook == IntPtr.Zero) return;
            Native.UnhookWindowsHookEx(hook);
            hook = IntPtr.Zero;
        }

        void Stop()
        {
            Unhook();
            placed = false;
            if (dim == null) return;
            tile.Hide();
            dim.Hide();
        }

        public void Dispose()
        {
            Stop();
            if (dim == null) return;
            tile.Dispose();
            dim.Dispose();
        }

        // The edge round the clear area (soft, or an outline), dimmed around it, as one per-pixel-alpha window.
        class Tile : Form
        {
            Rectangle clipped;   // the part of it shown, in its own coordinates (only set again when that changes)

            public Tile()
            {
                FormBorderStyle = FormBorderStyle.None;
                ShowInTaskbar = false;
                StartPosition = FormStartPosition.Manual;
            }

            protected override bool ShowWithoutActivation { get { return true; } }

            protected override CreateParams CreateParams
            {
                get
                {
                    CreateParams cp = base.CreateParams;
                    cp.ExStyle |= Native.WS_EX_LAYERED | Native.WS_EX_TRANSPARENT | Native.WS_EX_TOOLWINDOW | Native.WS_EX_NOACTIVATE | Native.WS_EX_TOPMOST;
                    return cp;
                }
            }

            // How far outside the clear area a pixel is (negative inside), for each shape; pad is the room round it.
            static double Distance(int shape, double x, double y, Size size, int pad)
            {
                double cx = size.Width / 2.0, cy = size.Height / 2.0;
                if (shape == 0) return Math.Abs(y - cy) - (cy - pad);
                if (shape == 1) return Math.Abs(x - cx) - (cx - pad);
                if (shape == 3) return Math.Sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy)) - (cx - pad);
                double hx = cx - pad, hy = cy - pad, r = Corner(hx, hy);   // a softly rounded rectangle
                double qx = Math.Abs(x - cx) - (hx - r), qy = Math.Abs(y - cy) - (hy - r);
                double ox = Math.Max(qx, 0), oy = Math.Max(qy, 0);
                return Math.Sqrt(ox * ox + oy * oy) + Math.Min(Math.Max(qx, qy), 0) - r;
            }

            public void Draw(Size size, int shape, int pad, int feather, int alpha, Color tint, int border, int cover, Color line)
            {
                int w = Math.Max(1, size.Width), h = Math.Max(1, size.Height);
                using (var bmp = new Bitmap(w, h, PixelFormat.Format32bppPArgb))
                {
                    BitmapData data = bmp.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.WriteOnly, PixelFormat.Format32bppPArgb);
                    var row = new int[w];
                    for (int y = 0; y < h; y++)
                    {
                        // a band's rows are all alike across: work out the first pixel, copy it on
                        for (int x = 0; x < w; x++)
                        {
                            if (shape == 0 && x > 0) { row[x] = row[0]; continue; }
                            double d = Distance(shape, x + 0.5, y + 0.5, size, pad);
                            bool before = shape == 0 ? y < h / 2 : x < w / 2;
                            double a;
                            if (feather > 0)
                            {
                                a = d <= 0 ? 0 : Math.Min(1, d / feather);
                                a = a * a * (3 - 2 * a);                           // smoothstep: no visible start or end
                            }
                            else a = Math.Max(0, Math.Min(1, d + 0.5));            // a crisp, anti-aliased edge
                            a *= alpha;
                            if ((cover == 1 && !before) || (cover == 2 && before)) a = 0;
                            double r = tint.R * a / 255, g = tint.G * a / 255, b = tint.B * a / 255;
                            if (border > 0 && d > -0.5 && d <= border + 0.5)
                            {
                                double k = Math.Min(1, Math.Min(d + 0.5, border + 0.5 - d)) * 0.9;   // an anti-aliased line
                                a += (255 - a) * k;
                                r += (line.R - r) * k; g += (line.G - g) * k; b += (line.B - b) * k;
                            }
                            row[x] = ((int)a << 24) | ((int)r << 16) | ((int)g << 8) | (int)b;
                        }
                        Marshal.Copy(row, 0, data.Scan0 + y * data.Stride, w);
                    }
                    bmp.UnlockBits(data);
                    Native.UpdateLayered(Handle, bmp, Location, 255);
                    clipped = Rectangle.Empty;   // a new size: clip it again
                }
            }

            // Moves it to t, clipped to its screen so it never spills onto a neighbour.
            public void Place(Rectangle t, Rectangle screen)
            {
                Rectangle clip = Rectangle.Intersect(t, screen);
                clip.Offset(-t.X, -t.Y);
                if (clip != clipped)
                {
                    clipped = clip;
                    Native.ClipWindow(Handle, clip);
                }
                Native.MoveNoActivate(Handle, t.X, t.Y);
                if (!Visible) Show();
            }
        }
    }
}
