using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Windows.Forms;

namespace BlinkBuddy
{
    // Click-through, never-focused, per-pixel-alpha window that fades in, blinks once and fades out.
    // Its 30 ms animation timer only runs during those ~2 seconds.
    class EyePopup : Form
    {
        const int FrameMs = 30, TotalMs = 1800, FadeMs = 250, BlinkFrom = 500, BlinkTo = 1200;
        double fadeDepth;   // how dark the screen gets at the bottom of a blink (an on-screen shade, never the backlight)
        readonly Timer timer = new Timer();
        readonly List<Shade> shades = new List<Shade>();
        Bitmap canvas;
        Point at;
        int elapsed;
        float opacity;
        bool fadeScreen;

        public EyePopup()
        {
            FormBorderStyle = FormBorderStyle.None;
            ShowInTaskbar = false;
            StartPosition = FormStartPosition.Manual;
            timer.Interval = FrameMs;
            timer.Tick += delegate { Step(); };
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

        // instant skips the fade-in, so live tweaks of size, opacity and position show straight away.
        public void Play(Settings s, float scale, bool instant)
        {
            int w = (int)(s.IconSize * scale), h = (int)(w * 0.64f);
            if (canvas == null || canvas.Width != w)
            {
                if (canvas != null) canvas.Dispose();
                canvas = new Bitmap(w, h, PixelFormat.Format32bppArgb);
            }
            // Show it on the monitor the mouse is on, which is usually the one being looked at.
            Rectangle wa = Screen.FromPoint(Cursor.Position).WorkingArea;
            int m = (int)(24 * scale);
            switch (s.IconCorner)
            {
                case 0: at = new Point(wa.Left + m, wa.Top + m); break;
                case 1: at = new Point(wa.Right - w - m, wa.Top + m); break;
                case 2: at = new Point(wa.Left + m, wa.Bottom - h - m); break;
                case 3: at = new Point(wa.Right - w - m, wa.Bottom - h - m); break;
                default: at = new Point(wa.Left + (wa.Width - w) / 2, wa.Top + (wa.Height - h) / 2); break;
            }
            opacity = s.IconOpacity / 100f;
            fadeScreen = s.BlinkFade && !instant;
            fadeDepth = s.BlinkFadeDepth / 100.0;
            elapsed = instant ? FadeMs : 0;
            if (!Visible) Show();
            Native.BringToTopNoActivate(Handle);
            Render();
            timer.Start();
        }

        void Step()
        {
            elapsed += FrameMs;
            if (elapsed >= TotalMs)
            {
                timer.Stop();
                ShadeScreen(0);
                Hide();
                return;
            }
            Render();
        }

        // The whole display dims as the eye closes and comes back as it opens, like a real blink.
        void ShadeScreen(double closed)
        {
            if (closed <= 0.01)
            {
                foreach (Shade sh in shades) if (sh.Visible) sh.Hide();
                return;
            }
            Screen[] screens = Screen.AllScreens;
            if (shades.Count != screens.Length)
            {
                foreach (Shade sh in shades) sh.Close();
                shades.Clear();
                foreach (Screen sc in screens) shades.Add(new Shade(sc.Bounds, Color.Black, 0.01));
            }
            foreach (Shade sh in shades)
            {
                sh.Opacity = fadeDepth * closed;
                if (sh.Visible) continue;
                sh.Show();
                Native.BringToTopNoActivate(Handle);   // keep the eye above the dimming
            }
        }

        void Render()
        {
            float fade = Math.Min(1f, Math.Min(elapsed / (float)FadeMs, (TotalMs - elapsed) / (float)FadeMs));
            float open = 1f;
            if (elapsed > BlinkFrom && elapsed < BlinkTo)
                open = (float)Math.Abs(Math.Cos(Math.PI * (elapsed - BlinkFrom) / (BlinkTo - BlinkFrom)));
            if (fadeScreen) ShadeScreen(1 - open);
            using (Graphics g = Graphics.FromImage(canvas)) IconArt.DrawPopup(g, canvas.Width, canvas.Height, open);
            Native.UpdateLayered(Handle, canvas, at, (byte)(255 * opacity * Math.Max(0f, fade)));
        }
    }
}
