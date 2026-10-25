using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Windows.Forms;
using Timer = System.Windows.Forms.Timer;

namespace BlinkBuddy
{
    // Auto comfort: a no-sliders filter built only from stages the display hardware applies anyway, so it adds no
    // delay and no per-frame work.
    //  - Gamma curve x^g: pushes darks down while spreading bright shades apart, so the screen gets darker but text
    //    and edges get crisper. Every channel is raised to the same power, which keeps hues (colours get slightly
    //    richer, never greyer).
    //  - Colour-effect scale: lowers whites and bright colours evenly, which keeps every colour's proportions exactly.
    //  - Every two seconds it looks at a 32x20 thumbnail of the screen; mostly bright content (white pages) is dimmed
    //    up to 15% more, eased in over about a second.
    class Comfort : IDisposable
    {
        // Shared by every engine; each engine's numbers are set so a level puts white at about the same place.
        public static readonly string[] Levels = { "Gentle", "Balanced", "Strong", "Max" };
        static readonly double[] Gammas = { 1.15, 1.3, 1.45, 1.45 };   // Max dims through its white level: a steeper curve would bury dark grey
        static readonly double[] Whites = { 0.85, 0.7, 0.55, 0.4 };   // where pure white ends up
        const double MaxExtraDim = 0.15;

        [DllImport("user32.dll")] static extern IntPtr GetDC(IntPtr hwnd);
        [DllImport("user32.dll")] static extern int ReleaseDC(IntPtr hwnd, IntPtr dc);
        [DllImport("gdi32.dll")] static extern int SetStretchBltMode(IntPtr dc, int mode);
        [DllImport("gdi32.dll")] static extern bool StretchBlt(IntPtr dst, int x, int y, int w, int h, IntPtr src, int sx, int sy, int sw, int sh, int rop);

        readonly Timer sampler = new Timer { Interval = 2000 };
        readonly Timer easing = new Timer { Interval = 50 };
        readonly Bitmap thumb = new Bitmap(32, 20, PixelFormat.Format32bppRgb);
        readonly int[] pixels = new int[32 * 20];
        Settings s;
        double adapt = 1, target = 1;
        public Action Changed;               // the scale moved; reapply the colour effect
        public Func<float> CurrentGain;      // how much the colour effect dims right now, undone when measuring

        public Comfort()
        {
            sampler.Tick += delegate { Sample(); };
            easing.Tick += delegate { Ease(); };
        }

        public static double Gamma(Settings s) { return Gammas[s.Level]; }

        public double White { get { return Whites[s.Level] * adapt; } }
        public bool Adapting { get { return adapt < 0.995; } }

        // The colour effect runs before the gamma curve, so it scales by White^(1/g) for white to land on White.
        public float Scale { get { return (float)Math.Pow(White, 1 / Gamma(s)); } }

        // Input brightness -> what is shown, for the graph.
        public double Net(double x) { return Math.Pow(Scale * x, Gamma(s)); }

        public void Apply(Settings settings, bool active)
        {
            s = settings;
            bool run = active && s.AdaptToContent;
            sampler.Enabled = run;
            if (!run)
            {
                easing.Stop();
                adapt = target = 1;
            }
        }

        public void Dispose()
        {
            sampler.Dispose();
            easing.Dispose();
            thumb.Dispose();
        }

        void Sample()
        {
            double mean = ScreenMean() / Math.Max(0.05f, CurrentGain());
            target = 1 - MaxExtraDim * Math.Max(0, Math.Min(1, (mean - 0.35) / 0.45));
            if (Math.Abs(target - adapt) > 0.01) easing.Start();
        }

        void Ease()
        {
            adapt += Math.Max(-0.01, Math.Min(0.01, target - adapt));
            if (Math.Abs(target - adapt) < 0.001)
            {
                adapt = target;
                easing.Stop();
            }
            if (Changed != null) Changed();
        }

        // Average brightness (0..1) of the main screen from a point-sampled 32x20 thumbnail.
        double ScreenMean()
        {
            Rectangle b = Screen.PrimaryScreen.Bounds;
            IntPtr screen = GetDC(IntPtr.Zero);
            using (Graphics g = Graphics.FromImage(thumb))
            {
                IntPtr dc = g.GetHdc();
                SetStretchBltMode(dc, 3);   // COLORONCOLOR
                StretchBlt(dc, 0, 0, 32, 20, screen, b.Left, b.Top, b.Width, b.Height, 0x00CC0020);   // SRCCOPY
                g.ReleaseHdc(dc);
            }
            ReleaseDC(IntPtr.Zero, screen);
            BitmapData data = thumb.LockBits(new Rectangle(0, 0, 32, 20), ImageLockMode.ReadOnly, PixelFormat.Format32bppRgb);
            Marshal.Copy(data.Scan0, pixels, 0, pixels.Length);
            thumb.UnlockBits(data);
            double sum = 0;
            foreach (int p in pixels)
                sum += 0.2126 * ((p >> 16) & 255) + 0.7152 * ((p >> 8) & 255) + 0.0722 * (p & 255);
            return sum / (255.0 * pixels.Length);
        }
    }
}
