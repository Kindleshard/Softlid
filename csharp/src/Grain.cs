using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Windows.Forms;
using Timer = System.Windows.Forms.Timer;

namespace BlinkBuddy
{
    // Film grain: fine monochrome noise in a click-through window over each screen. It is drawn once. When animated,
    // the window is nudged to a random offset 24 times a second; the noise is random, so a shifted copy looks like
    // fresh grain, and a window move costs far less than redrawing millions of pixels.
    class Grain : IDisposable
    {
        const int Spare = 48;   // spare noise around the screen, so shifted copies still cover it
        readonly List<GrainWindow> windows = new List<GrainWindow>();
        readonly Timer jitter = new Timer { Interval = 42 };
        readonly Timer rebuild = new Timer { Interval = 150 };   // slider drags settle before a redraw
        readonly Random random = new Random();
        Settings settings;
        float scale;
        string key = "off";

        public Grain()
        {
            jitter.Tick += delegate { Jitter(); };
            rebuild.Tick += delegate
            {
                rebuild.Stop();
                Build();
            };
        }

        // baked: the smart layer is drawing the grain itself, so these windows stand down.
        public void Apply(Settings s, float dpiScale, bool baked)
        {
            settings = s;
            scale = dpiScale;
            bool on = s.GrainEnabled && !baked;
            string k = on ? s.GrainStrength + "," + s.GrainCoarse + "," + s.GrainTone : "off";
            if (k != key)
            {
                key = k;
                rebuild.Stop();
                if (!on) Close();
                else rebuild.Start();
            }
            jitter.Enabled = on && s.GrainAnimated;
        }

        // After a display change: redraw for the new screens.
        public void Invalidate() { key = null; }

        public void Dispose()
        {
            jitter.Dispose();
            rebuild.Dispose();
            Close();
        }

        void Build()
        {
            Close();
            int block = settings.GrainCoarse ? Math.Max(2, (int)Math.Round(2 * scale)) : 1;
            int maxAlpha = (int)Math.Round(settings.GrainStrength * 1.28);   // 100% = up to half-opaque specks
            foreach (Screen sc in Screen.AllScreens)
                windows.Add(new GrainWindow(sc.Bounds, block, maxAlpha, settings.GrainTone, random.Next()));
            // The noise now lives in the compositor; give back the ~20 MB used to draw it.
            GC.Collect();
            Native.TrimWorkingSet();
        }

        void Close()
        {
            foreach (GrainWindow w in windows) w.Close();
            windows.Clear();
        }

        void Jitter()
        {
            foreach (GrainWindow w in windows) w.Shift(random.Next(-Spare, Spare + 1), random.Next(-Spare, Spare + 1));
        }

        class GrainWindow : Form
        {
            readonly Rectangle screen;

            public GrainWindow(Rectangle screen, int block, int maxAlpha, int tone, int seed)
            {
                this.screen = screen;
                FormBorderStyle = FormBorderStyle.None;
                ShowInTaskbar = false;
                StartPosition = FormStartPosition.Manual;
                Show();   // a layered window stays invisible until it gets content
                using (Bitmap noise = Noise(screen.Width + 2 * Spare, screen.Height + 2 * Spare, block, maxAlpha, tone, seed))
                    Native.UpdateLayered(Handle, noise, new Point(screen.Left - Spare, screen.Top - Spare), 255);
                Shift(0, 0);
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

            // Moves the noise by (dx, dy) while clipping it to its own screen, so it never spills onto a neighbour.
            public void Shift(int dx, int dy)
            {
                Native.ClipWindow(Handle, new Rectangle(Spare - dx, Spare - dy, screen.Width, screen.Height));
                Native.MoveNoActivate(Handle, screen.Left - Spare + dx, screen.Top - Spare + dy);
            }

            // Each speck changes the screen by a random amount (triangular distribution). tone 1 (mixed) lightens or
            // darkens, keeping overall brightness; 0 only darkens and 2 only lightens.
            static Bitmap Noise(int w, int h, int block, int maxAlpha, int tone, int seed)
            {
                var bmp = new Bitmap(w, h, PixelFormat.Format32bppArgb);
                BitmapData data = bmp.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
                var row = new int[w];
                var specks = new int[(w + block - 1) / block];
                uint state = (uint)seed | 1;
                for (int y = 0; y < h; y++)
                {
                    if (y % block == 0)
                        for (int i = 0; i < specks.Length; i++)
                        {
                            state ^= state << 13; state ^= state >> 17; state ^= state << 5;   // xorshift
                            int n = (int)(state & 255) + (int)((state >> 8) & 255) - 255;    // -255..255
                            bool light = tone == 2 || (tone == 1 && n > 0);
                            specks[i] = ((Math.Abs(n) * maxAlpha / 255) << 24) | (light ? 0xFFFFFF : 0);
                        }
                    for (int x = 0; x < w; x++) row[x] = specks[x / block];
                    Marshal.Copy(row, 0, data.Scan0 + y * data.Stride, w);
                }
                bmp.UnlockBits(data);
                return bmp;
            }
        }
    }
}
