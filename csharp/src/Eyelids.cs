using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace BlinkBuddy
{
    // The immersive blink: on every screen, a dark lid slides down from the top and another up from the bottom, meet
    // (or nearly, by the depth setting) and part again, like your own eyelids. Each lid is drawn once per screen size,
    // with a gently curved, soft rim, and the animation only moves the two windows, so it costs next to nothing.
    class Eyelids : IDisposable
    {
        const int FrameMs = 15, CloseMs = 260, HoldMs = 90, OpenMs = 420;
        readonly Timer timer = new Timer { Interval = FrameMs };
        readonly List<Lid[]> pairs = new List<Lid[]>();
        readonly System.Diagnostics.Stopwatch clock = new System.Diagnostics.Stopwatch();
        Rectangle[] screens = new Rectangle[0];
        double depth;

        public Eyelids()
        {
            timer.Tick += delegate { Step(); };
        }

        public void Play(Settings s)
        {
            depth = Math.Min(1, s.BlinkFadeDepth / 100.0);   // 100% (the slider's end) closes them all the way
            Rectangle[] now = Array.ConvertAll(Screen.AllScreens, sc => sc.Bounds);
            if (!Same(now, screens))
            {
                Close();
                screens = now;
                foreach (Rectangle r in screens) pairs.Add(new[] { new Lid(r, true), new Lid(r, false) });
            }
            clock.Restart();
            Place(0);
            foreach (Lid[] p in pairs) foreach (Lid l in p) l.ShowAbove();
            timer.Start();
        }

        static bool Same(Rectangle[] a, Rectangle[] b)
        {
            if (a.Length != b.Length) return false;
            for (int i = 0; i < a.Length; i++) if (a[i] != b[i]) return false;
            return true;
        }

        void Step()
        {
            double t = clock.Elapsed.TotalMilliseconds, c;
            if (t < CloseMs) c = Ease(t / CloseMs);
            else if (t < CloseMs + HoldMs) c = 1;
            else if (t < CloseMs + HoldMs + OpenMs) c = 1 - Ease((t - CloseMs - HoldMs) / OpenMs);
            else
            {
                timer.Stop();
                foreach (Lid[] p in pairs) foreach (Lid l in p) l.Hide();
                return;
            }
            Place(c * depth);
        }

        // Smooth start and stop, like a real lid.
        static double Ease(double x) { return x * x * (3 - 2 * x); }

        // closed: 0 = lids out of sight, 1 = their edges meet in the middle.
        void Place(double closed)
        {
            foreach (Lid[] p in pairs)
            {
                p[0].MoveTo(closed);
                p[1].MoveTo(closed);
            }
        }

        void Close()
        {
            foreach (Lid[] p in pairs) foreach (Lid l in p) l.Dispose();
            pairs.Clear();
        }

        public void Dispose()
        {
            timer.Dispose();
            Close();
        }

        // One lid: a click-through, never-focused, per-pixel-alpha window half a screen tall plus its soft rim.
        class Lid : Form
        {
            readonly Rectangle screen;
            readonly bool top;
            readonly int half, rim;
            readonly Bitmap art;

            public Lid(Rectangle screen, bool top)
            {
                this.screen = screen;
                this.top = top;
                half = screen.Height / 2;
                rim = Math.Max(24, screen.Height / 9);
                FormBorderStyle = FormBorderStyle.None;
                ShowInTaskbar = false;
                StartPosition = FormStartPosition.Manual;
                art = Draw(screen.Width, half, rim, top);
                Bounds = new Rectangle(screen.X, Y(0), screen.Width, art.Height);
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

            // Out of sight at 0; at 1 the dark part reaches the middle and the rim spills just past it.
            int Y(double closed)
            {
                int travel = half + rim;
                return top ? screen.Y - travel + (int)Math.Round(closed * travel)
                           : screen.Bottom - (int)Math.Round(closed * travel);
            }

            public void ShowAbove()
            {
                if (!Visible)
                {
                    Show();
                    Native.UpdateLayered(Handle, art, new Point(screen.X, Y(0)), 255);
                }
                Native.BringToTopNoActivate(Handle);
            }

            public void MoveTo(double closed)
            {
                if (Visible) Native.MoveNoActivate(Handle, screen.X, Y(closed));
            }

            protected override void Dispose(bool disposing)
            {
                if (disposing) art.Dispose();
                base.Dispose(disposing);
            }

            // Near-black (a lid lets a little warm light through), with an edge that dips toward the middle like a
            // real lid and fades out over the rim. Premultiplied alpha, as UpdateLayeredWindow wants.
            static Bitmap Draw(int w, int half, int rim, bool top)
            {
                int h = half + rim;
                var bmp = new Bitmap(w, h, PixelFormat.Format32bppPArgb);
                var px = new int[w * h];
                double curve = rim * 0.55;   // how much lower the edge sits in the middle than at the sides
                for (int x = 0; x < w; x++)
                {
                    double u = 2.0 * x / (w - 1) - 1, edge = half + curve * (1 - u * u) - curve;   // distance of the edge from the lid's base
                    for (int y = 0; y < h; y++)
                    {
                        double d = (top ? y : h - 1 - y) - edge;   // > 0 past the edge
                        double a = d <= 0 ? 1 : d >= rim ? 0 : 1 - Smooth(d / rim);
                        int alpha = (int)Math.Round(250 * a);
                        // premultiplied (8, 6, 5)
                        px[y * w + x] = (alpha << 24) | ((8 * alpha / 255) << 16) | ((6 * alpha / 255) << 8) | (5 * alpha / 255);
                    }
                }
                BitmapData data = bmp.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.WriteOnly, PixelFormat.Format32bppPArgb);
                Marshal.Copy(px, 0, data.Scan0, px.Length);
                bmp.UnlockBits(data);
                return bmp;
            }

            static double Smooth(double x) { return x * x * (3 - 2 * x); }
        }
    }
}
