using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Windows.Forms;

namespace BlinkBuddy
{
    // Mini mode: the settings window folded into a round eye that stays on top. Hovering opens a small panel beside it
    // with brightness, the blue light filter, the backlight (when that is on) and the blink reminder switch; clicking
    // the eye opens the full settings again; dragging moves it. The eye is a per-pixel-alpha window, so its circle is
    // smooth on any background. Left alone, it fades to 65% (after the delay and over the time set in Options).
    class MiniWindow : Form
    {
        const int Size96 = 56, Gap96 = 10;
        const byte Faded = 166;   // 65% of 255
        readonly App app;
        readonly Settings s;
        readonly Panel96 panel;
        readonly Timer hover = new Timer { Interval = 120 };
        readonly Timer fade = new Timer { Interval = 15 };   // runs only while waiting to fade and fading
        readonly Timer anim = new Timer { Interval = 15 };   // runs only while the icon animates or the orbit light runs
        readonly int size, gap;
        Bitmap art;
        bool lit, pressed, dragged;
        Point grab;
        DateTime idleSince = DateTime.Now, lastAnim, nextOrbit, orbitStart = DateTime.MinValue;
        double shown = 255;
        double awake;                  // the icon's hover animation, 0 resting .. 1 hovered
        float drawnBright = -1, drawnWarm = -1, drawnProgress = -2;
        int drawnIcon = -1, tick;
        string drawnClock;
        const double OrbitSeconds = 3.6;   // one breath

        public MiniWindow(App app)
        {
            this.app = app;
            s = app.S;
            FormBorderStyle = FormBorderStyle.None;
            ShowInTaskbar = false;
            StartPosition = FormStartPosition.Manual;
            Text = "BlinkBuddy";
            Cursor = Cursors.Hand;
            float scale;
            using (Graphics g = Graphics.FromHwnd(IntPtr.Zero)) scale = g.DpiX / 96f;
            size = (int)Math.Round(Size96 * scale);
            gap = (int)Math.Round(Gap96 * scale);
            Bounds = new Rectangle(StartingPoint(), new Size(size, size));
            panel = new Panel96(app);
            hover.Tick += delegate { CheckHover(); };
            fade.Tick += delegate { FadeStep(); };
            anim.Tick += delegate { AnimStep(); };
            nextOrbit = DateTime.Now.AddSeconds(Math.Max(3, s.MiniOrbitSeconds));
        }

        protected override bool ShowWithoutActivation { get { return true; } }

        protected override CreateParams CreateParams
        {
            get
            {
                CreateParams cp = base.CreateParams;
                cp.ExStyle |= Native.WS_EX_LAYERED | Native.WS_EX_TOOLWINDOW | Native.WS_EX_TOPMOST;
                return cp;
            }
        }

        protected override void OnLoad(EventArgs e)
        {
            base.OnLoad(e);
            Draw();
            hover.Start();
            fade.Start();   // nobody is hovering yet: fade after the delay
        }

        protected override void OnFormClosed(FormClosedEventArgs e)
        {
            hover.Dispose();
            fade.Dispose();
            anim.Dispose();
            panel.Close();
            if (art != null) art.Dispose();
            if (clockFont != null) { clockFont.Dispose(); clockFontBig.Dispose(); pauseFont.Dispose(); }
            base.OnFormClosed(e);
        }

        // Where it was left, if that is still on a screen; otherwise near the top-right corner of the main screen.
        Point StartingPoint()
        {
            var at = new Point(s.MiniX, s.MiniY);
            foreach (Screen sc in Screen.AllScreens)
                if (s.MiniX >= 0 && sc.WorkingArea.Contains(new Rectangle(at, new Size(size, size)))) return at;
            Rectangle wa = Screen.PrimaryScreen.WorkingArea;
            return new Point(wa.Right - size - size / 2, wa.Top + size / 2);
        }

        // What the lamp and the laptop show: the screen's brightness (0 at its lowest, 1 at full) and warmth.
        float Bright { get { return s.FilterEnabled ? (s.Brightness - 20) / 80f : 1f; } }

        // A dark disc with a soft rim (blue while hovered), the chosen icon inside in its current state, and the orbit
        // light when it is running; anti-aliased against whatever is behind. The window's own fade (shown) is separate.
        void Draw()
        {
            if (art == null) art = new Bitmap(size, size, PixelFormat.Format32bppPArgb);
            float k = (float)(awake * awake * (3 - 2 * awake));
            drawnBright = Bright;
            drawnWarm = app.WarmthNow();
            drawnIcon = s.MiniIcon;
            drawnProgress = Progress();
            using (Graphics g = Graphics.FromImage(art))
            {
                g.SmoothingMode = SmoothingMode.AntiAlias;
                g.Clear(Color.Transparent);
                float ring = Math.Max(1.5f, size / 30f);
                var disc = new RectangleF(ring / 2, ring / 2, size - ring - 1, size - ring - 1);
                using (var b = new SolidBrush(Color.FromArgb(242, 34, 36, 42))) g.FillEllipse(b, disc);
                Color rest = Color.FromArgb(120, 90, 96, 110), on = Theme.Accent;
                Color rim = Color.FromArgb((int)(rest.A + (on.A - rest.A) * k), (int)(rest.R + (on.R - rest.R) * k), (int)(rest.G + (on.G - rest.G) * k), (int)(rest.B + (on.B - rest.B) * k));
                using (var pen = new Pen(rim, ring)) g.DrawEllipse(pen, disc);
                float box = size * 0.62f;
                drawnClock = FocusOn ? app.FocusText() : null;
                if (drawnClock != null) DrawClock(g, drawnClock);   // the focus timer's countdown in place of the icon
                else IconArt.DrawMiniIcon(g, new RectangleF((size - box) / 2, (size - box) / 2, box, box), s.MiniIcon, k, drawnBright, drawnWarm);
                // the wait for the next break (or the end of a focus period), as an arc filling round the rim from the top
                if (drawnProgress > 0.002f)
                {
                    using (var pen = new Pen(Color.FromArgb(190, Theme.Accent), ring * 1.15f) { StartCap = LineCap.Round, EndCap = LineCap.Round })
                        Arc(g, pen, disc, -90, 360 * drawnProgress);
                    if (Flowing) DrawFlow(g, disc, ring, drawnProgress);
                }
                if (!FocusOn) DrawOrbit(g, disc, ring);   // (the focus timer's flow takes the breath's place)
            }
            Native.UpdateLayered(Handle, art, Location, (byte)Math.Round(shown));
        }

        Font clockFont, clockFontBig, pauseFont;
        static readonly StringFormat Centre = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center };

        // The focus timer's time left, in the middle; paused, it is dimmed with a small pause sign under it.
        void DrawClock(Graphics g, string time)
        {
            g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;
            bool paused = app.FocusState == App.FocusPaused;
            // (the fonts are made once: this is drawn up to 30 times a second)
            if (clockFont == null)
            {
                clockFont = new Font("Segoe UI Semibold", size * 0.23f, GraphicsUnit.Pixel);
                clockFontBig = new Font("Segoe UI Semibold", size * 0.27f, GraphicsUnit.Pixel);
                pauseFont = new Font(Theme.GlyphFamily, size * 0.13f, GraphicsUnit.Pixel);
            }
            using (var ink = new SolidBrush(paused ? Theme.Dim : Theme.Fg))
                g.DrawString(time, time.Length > 4 ? clockFont : clockFontBig, ink, new RectangleF(0, paused ? -size * 0.06f : 0, size, size), Centre);
            if (!paused) return;
            using (var ink = new SolidBrush(Theme.Dim))
                g.DrawString("", pauseFont, ink, new RectangleF(0, size * 0.56f, size, size * 0.2f), Centre);
        }

        // GDI+ fails ("out of memory") on an arc too short to see, as the flow's trail is at the start of each run: those
        // are left out.
        static void Arc(Graphics g, Pen pen, RectangleF r, float start, float sweep)
        {
            if (sweep < 0.5f) return;
            try { g.DrawArc(pen, r, start, sweep); }
            catch (OutOfMemoryException) { }
        }

        // The rim's arc: the wait for the next break when that is shown, and always while the focus timer is on.
        float Progress() { return s.MiniCountdown || FocusOn ? app.BreakProgress() : -1; }
        bool FocusOn { get { return app.FocusState == App.FocusWorking || app.FocusState == App.FocusPaused; } }
        bool Flowing { get { return app.FocusState == App.FocusWorking; } }
        const double FlowSeconds = 1.7;   // one run of the highlight, splash included

        // While the focus timer runs, a highlight flows along the filled arc like light caught in a liquid: it gathers at
        // the top, runs faster and faster (stretching as it goes) to the arc's end, splashes against it, and starts again.
        void DrawFlow(Graphics g, RectangleF disc, float ring, float progress)
        {
            double t = (DateTime.Now - DateTime.Today).TotalSeconds % FlowSeconds / FlowSeconds;
            float sweep = 360 * progress;
            Color a = Theme.Accent, glint = Color.FromArgb(a.R + (255 - a.R) * 2 / 3, a.G + (255 - a.G) * 2 / 3, a.B + (255 - a.B) * 2 / 3);
            const double run = 0.7;
            if (t < run)
            {
                double u = t / run;
                float head = sweep * (float)(u * u);                                  // accelerating, like a drop running down
                float trail = Math.Min(sweep, 80f) * (float)(0.35 + 0.65 * u);        // longer the faster it goes
                float fadeIn = (float)Math.Min(1, u * 6);
                const int parts = 10;
                for (int i = 0; i < parts; i++)
                {
                    float end = head - trail * i / parts, start = Math.Max(0f, head - trail * (i + 1) / parts);
                    if (end <= start) break;
                    float k = 1f - i / (float)parts;
                    using (var pen = new Pen(Color.FromArgb((int)(235 * k * k * fadeIn), glint), ring * (0.7f + 0.5f * k)) { StartCap = LineCap.Round, EndCap = LineCap.Round })
                        Arc(g, pen, disc, -90 + start, end - start);
                }
                return;
            }
            // the splash at the end: a bright bead that swells into a fading ring, and the arc's end lit for a moment
            float s1 = (float)((t - run) / (1 - run)), fade = (1 - s1) * (1 - s1);
            double ang = (-90 + sweep) * Math.PI / 180;
            float px = disc.X + disc.Width / 2 + (float)Math.Cos(ang) * disc.Width / 2, py = disc.Y + disc.Height / 2 + (float)Math.Sin(ang) * disc.Height / 2;
            float tip = Math.Min(sweep, 16f);
            using (var pen = new Pen(Color.FromArgb((int)(220 * fade), glint), ring * 1.2f) { StartCap = LineCap.Round, EndCap = LineCap.Round })
                Arc(g, pen, disc, -90 + sweep - tip, tip);
            float bead = ring * (1.1f - 0.4f * s1), wave = ring * (1.2f + 2.8f * s1);
            using (var b = new SolidBrush(Color.FromArgb((int)(240 * fade), glint))) g.FillEllipse(b, px - bead, py - bead, 2 * bead, 2 * bead);
            using (var pen = new Pen(Color.FromArgb((int)(170 * fade), glint), Math.Max(1f, ring * 0.6f))) g.DrawEllipse(pen, px - wave, py - wave, 2 * wave, 2 * wave);
        }

        // Now and then the icon breathes: its rim swells into a soft glow, which spreads a little way inward, and
        // eases back out - a slow breath in, a longer breath out.
        void DrawOrbit(Graphics g, RectangleF disc, float ring)
        {
            if (orbitStart == DateTime.MinValue) return;
            double p = (DateTime.Now - orbitStart).TotalSeconds / OrbitSeconds;
            if (p >= 1) return;
            double inhale = 0.4;
            float breath = (float)(p < inhale ? 0.5 - 0.5 * Math.Cos(Math.PI * p / inhale) : 0.5 + 0.5 * Math.Cos(Math.PI * (p - inhale) / (1 - inhale)));
            if (breath < 0.01f) return;
            Color c = s.MiniOrbitColor == 0 ? Theme.Accent : Theme.Rgb(s.MiniOrbitColor);
            // the glow inside the rim, fading toward the middle so the icon stays clear
            using (var path = new GraphicsPath())
            {
                path.AddEllipse(disc);
                using (var b = new PathGradientBrush(path)
                {
                    CenterColor = Color.FromArgb(0, c),
                    SurroundColors = new[] { Color.FromArgb((int)(110 * breath), c) },
                    FocusScales = new PointF(0.62f, 0.62f)
                })
                    g.FillPath(b, path);
            }
            // and the rim itself, brighter and a little fuller
            using (var pen = new Pen(Color.FromArgb((int)(235 * breath), c), ring * (1f + 0.7f * breath))) g.DrawEllipse(pen, disc);
        }

        void SetLit(bool on)
        {
            if (lit == on) return;
            lit = on;
            lastAnim = DateTime.Now;
            anim.Start();
        }

        // Moves the hover animation toward lit (about a quarter of a second) and keeps the orbit going.
        void AnimStep()
        {
            DateTime now = DateTime.Now;
            double dt = Math.Min(0.05, (now - lastAnim).TotalSeconds);
            lastAnim = now;
            double target = lit ? 1 : 0, speed = lit ? 4.5 : 2.6;   // wakes quickly, settles back more slowly
            awake = target > awake ? Math.Min(target, awake + speed * dt) : Math.Max(target, awake - speed * dt);
            bool orbiting = orbitStart != DateTime.MinValue && (now - orbitStart).TotalSeconds < OrbitSeconds;
            if (!orbiting) orbitStart = DateTime.MinValue;
            bool flowing = Flowing && drawnProgress > 0.002f;
            Draw();
            anim.Interval = awake != target ? 15 : 33;   // the hover animation at 60 frames a second; a slow breath or the flow at 30
            if (awake == target && !orbiting && !flowing) anim.Stop();
        }

        // Back to full strength at once when the pointer arrives (the panel too, if it was fading out).
        void Wake()
        {
            idleSince = DateTime.MaxValue;
            if (shown < 255) { shown = 255; Draw(); }
            if (panel.Visible && panel.Opacity < 1) panel.Opacity = 1;
            if (!panel.Visible) fade.Stop();
        }

        // When the pointer has left: after the delay set in Options, the panel fades out over the time set there
        // (and closes), and the eye fades to 65% with it.
        void Rest()
        {
            if (idleSince != DateTime.MaxValue) return;
            idleSince = DateTime.Now;
            fade.Start();
        }

        // Every 15 ms while the panel is open or a fade is due: watches the pointer closely (so a short delay is
        // really short), and moves the fade along.
        void FadeStep()
        {
            if (panel.Visible && !pressed)
            {
                Point at = Cursor.Position;
                bool here = Bounds.Contains(at) || Rectangle.Inflate(panel.Bounds, gap, gap).Contains(at)
                    || (Control.MouseButtons & MouseButtons.Left) != 0;   // a slider being dragged keeps it open
                if (here) { Wake(); return; }
                Rest();
            }
            if (idleSince == DateTime.MaxValue) return;
            double since = (DateTime.Now - idleSince).TotalMilliseconds - s.MiniFadeDelay;
            if (since < 0) return;
            double k = s.MiniFadeTime <= 0 ? 1 : Math.Min(1, since / s.MiniFadeTime), e = k * k * (3 - 2 * k);
            shown = 255 - (255 - Faded) * e;
            Draw();
            if (panel.Visible)
            {
                if (k >= 1) { panel.Hide(); panel.Opacity = 1; SetLit(false); }
                else panel.Opacity = 1 - e;
            }
            if (k >= 1) fade.Stop();
        }

        protected override void OnMouseEnter(EventArgs e)
        {
            base.OnMouseEnter(e);
            Wake();
        }

        protected override void OnMouseLeave(EventArgs e)
        {
            base.OnMouseLeave(e);
            if (!panel.Visible && !pressed) Rest();
        }

        protected override void OnMouseDown(MouseEventArgs e)
        {
            base.OnMouseDown(e);
            if (e.Button != MouseButtons.Left) return;
            pressed = true;
            dragged = false;
            grab = Cursor.Position;
        }

        protected override void OnMouseMove(MouseEventArgs e)
        {
            base.OnMouseMove(e);
            if (!pressed) return;
            Point now = Cursor.Position;
            if (!dragged && Math.Abs(now.X - grab.X) + Math.Abs(now.Y - grab.Y) < 5) return;
            if (!dragged) panel.Hide();
            dragged = true;
            Location = new Point(Location.X + now.X - grab.X, Location.Y + now.Y - grab.Y);
            grab = now;
        }

        protected override void OnMouseUp(MouseEventArgs e)
        {
            base.OnMouseUp(e);
            if (!pressed) return;
            pressed = false;
            if (dragged) app.SaveMiniPosition(Location);
            else app.LeaveMiniMode();   // a plain click on the eye opens the full settings
        }

        void CheckHover()
        {
            // The orbit light, when it is due.
            if (s.MiniOrbit && !FocusOn && DateTime.Now >= nextOrbit)
            {
                nextOrbit = DateTime.Now.AddSeconds(s.MiniOrbitSeconds == 0 ? OrbitSeconds : s.MiniOrbitSeconds);   // 0: the next round follows right on
                orbitStart = lastAnim = DateTime.Now;
                anim.Start();
            }
            // The focus timer's flow keeps the animation going while it runs.
            if (Flowing && !anim.Enabled) { lastAnim = DateTime.Now; anim.Start(); }
            // Brightness, the filter or the chosen icon changed (hotkeys, the panel, the schedule): show it.
            if (!anim.Enabled && (Math.Abs(Bright - drawnBright) > 0.005f || Math.Abs(app.WarmthNow() - drawnWarm) > 0.005f || s.MiniIcon != drawnIcon
                || Math.Abs(Progress() - drawnProgress) > 0.004f || (FocusOn ? app.FocusText() : null) != drawnClock))
                Draw();
            // Stay above other windows, even ones that made themselves topmost since.
            if (++tick % 16 == 0) Native.BringToTopNoActivate(Handle);
            if (pressed) return;
            Point at = Cursor.Position;
            bool onEye = Bounds.Contains(at);
            bool onPanel = panel.Visible && Rectangle.Inflate(panel.Bounds, gap, gap).Contains(at);
            SetLit(onEye || onPanel);
            if (onEye || onPanel) Wake();
            if (onEye && !panel.Visible) OpenPanel();
            if (panel.Visible) panel.SetStatus(app.FocusState != App.FocusOff ? Lang.F("Focus timer: {0}", app.FocusText()) : app.BreakStatus(), app.FocusState);
            if (!panel.Visible && !onEye) Rest();
            // While the panel is open, FadeStep watches the pointer and fades it out.
        }

        // Beside the eye, toward the middle of the screen, centred on it vertically.
        void OpenPanel()
        {
            Rectangle wa = Screen.FromPoint(Location).WorkingArea;
            bool left = Left + size / 2 > wa.Left + wa.Width / 2;
            panel.Sync();
            int x = left ? Left - gap - panel.Width : Right + gap;
            int y = Math.Max(wa.Top, Math.Min(wa.Bottom - panel.Height, Top + size / 2 - panel.Height / 2));
            panel.Location = new Point(x, y);
            panel.Opacity = 1;
            panel.Show();
            Native.BringToTopNoActivate(panel.Handle);
            idleSince = DateTime.MaxValue;
            fade.Start();
        }

        // The panel: a rounded card with the three quick controls.
        class Panel96 : Form
        {
            readonly Settings s;
            readonly App app;
            readonly Slider bright, blue, light;
            readonly Label brightLabel, brightText, blueLabel, blueText, lightLabel, lightText, blinkLabel, statusLabel;
            readonly Toggle blink;
            readonly RoundButton focusPause, focusStop;   // the focus timer's buttons, beside the status line
            readonly ToolTip tips = new ToolTip();
            string shownTip;
            bool syncing;

            public Panel96(App app)
            {
                this.app = app;
                s = app.S;
                SuspendLayout();
                AutoScaleDimensions = new SizeF(96f, 96f);
                AutoScaleMode = AutoScaleMode.Dpi;
                FormBorderStyle = FormBorderStyle.None;
                ShowInTaskbar = false;
                StartPosition = FormStartPosition.Manual;
                BackColor = Theme.Card;
                ForeColor = Theme.Fg;
                Font = Theme.Body;
                ClientSize = new Size(300, 200);
                brightLabel = Theme.MakeLabel("Brightness", Theme.Small, Theme.Fg, new Rectangle(16, 10, 180, 18));
                Controls.Add(brightLabel);
                brightText = Theme.MakeLabel("0%", Theme.Small, Theme.Dim, new Rectangle(196, 10, 90, 18));
                brightText.TextAlign = ContentAlignment.MiddleRight;
                Controls.Add(brightText);
                bright = new Slider(20, 100, s.Brightness) { Bounds = new Rectangle(8, 28, 286, 28), BackColor = Theme.Card };
                Controls.Add(bright);
                blueLabel = Theme.MakeLabel("Blue light filter", Theme.Small, Theme.Fg, new Rectangle(16, 60, 180, 18));
                Controls.Add(blueLabel);
                blueText = Theme.MakeLabel("Off", Theme.Small, Theme.Dim, new Rectangle(196, 60, 90, 18));
                blueText.TextAlign = ContentAlignment.MiddleRight;
                Controls.Add(blueText);
                blue = new Slider(s.CoolRange ? -75 : 0, 100, s.BlueFilter) { Bounds = new Rectangle(8, 78, 286, 28), BackColor = Theme.Card, Warm = true };
                Controls.Add(blue);
                lightLabel = Theme.MakeLabel("Display backlight", Theme.Small, Theme.Fg, new Rectangle(16, 110, 180, 18));
                Controls.Add(lightLabel);
                lightText = Theme.MakeLabel("0%", Theme.Small, Theme.Dim, new Rectangle(196, 110, 90, 18));
                lightText.TextAlign = ContentAlignment.MiddleRight;
                Controls.Add(lightText);
                light = new Slider(0, 100, Math.Max(0, s.Backlight)) { Bounds = new Rectangle(8, 128, 286, 28), BackColor = Theme.Card };
                Controls.Add(light);
                blinkLabel = Theme.MakeLabel("Blink reminders", Theme.Small, Theme.Fg, new Rectangle(16, 166, 180, 24));
                Controls.Add(blinkLabel);
                blink = new Toggle(s.BlinkEnabled) { Location = new Point(244, 167), BackColor = Theme.Card };
                Controls.Add(blink);
                statusLabel = Theme.MakeLabel("Next break in 0:00", Theme.Small, Theme.Dim, new Rectangle(16, 200, 268, 20));
                Controls.Add(statusLabel);
                focusPause = new RoundButton { Text = "", Font = Theme.Glyphs, Bounds = new Rectangle(226, 197, 28, 28), BackColor = Theme.Card, Visible = false, TabStop = false };
                focusStop = new RoundButton { Text = "", Font = Theme.Glyphs, Bounds = new Rectangle(260, 197, 28, 28), BackColor = Theme.Card, Visible = false, TabStop = false };
                focusPause.Click += delegate
                {
                    if (app.FocusState == App.FocusWorking) app.FocusPause(); else app.FocusStart();
                    SetStatus(Lang.F("Focus timer: {0}", app.FocusText()), app.FocusState);
                };
                focusStop.Click += delegate { app.FocusStop(); SetStatus(app.BreakStatus(), app.FocusState); };
                tips.SetToolTip(focusStop, Lang.T("Stop"));
                Controls.Add(focusPause);
                Controls.Add(focusStop);
                ResumeLayout(false);

                light.ValueChanged += delegate
                {
                    if (syncing) return;
                    s.Backlight = light.Value;
                    app.Changed(true);
                    ShowValues();
                };

                bright.ValueChanged += delegate
                {
                    if (syncing) return;
                    s.Brightness = bright.Value;
                    app.ManualScreenChange();
                    s.FilterEnabled = true;
                    app.Changed(true);
                    ShowValues();
                };
                blue.ValueChanged += delegate
                {
                    if (syncing) return;
                    s.BlueFilter = blue.Value;
                    s.ColorEffect = ScreenFilter.NoEffect;   // a colour effect and the blue light filter take turns
                    blue.Muted = false;
                    app.ManualScreenChange();
                    s.FilterEnabled = true;
                    app.PreviewNight();
                    app.Changed(true);
                    ShowValues();
                };
                blink.CheckedChanged += delegate
                {
                    if (syncing) return;
                    s.BlinkEnabled = blink.Checked;
                    app.Changed(false);
                };
            }

            protected override bool ShowWithoutActivation { get { return true; } }

            protected override CreateParams CreateParams
            {
                get
                {
                    CreateParams cp = base.CreateParams;
                    cp.ExStyle |= Native.WS_EX_TOOLWINDOW | Native.WS_EX_TOPMOST;
                    return cp;
                }
            }

            protected override void OnHandleCreated(EventArgs e)
            {
                base.OnHandleCreated(e);
                Native.RoundCorners(Handle);
            }

            protected override void OnFormClosed(FormClosedEventArgs e)
            {
                tips.Dispose();
                base.OnFormClosed(e);
            }

            // Hotkeys and the settings may have changed things since it was last open.
            public void Sync()
            {
                syncing = true;
                bright.Value = s.Brightness;
                blue.Value = s.BlueFilter;
                blue.Marker = s.ScheduleEnabled ? s.NightBlue : -1;   // the night schedule's strength
                blue.Muted = s.FilterEnabled && s.ColorEffect != ScreenFilter.NoEffect && app.EffectsAvailable;   // paused by a colour effect
                blink.Checked = s.BlinkEnabled;
                light.Value = Math.Max(0, s.Backlight);
                syncing = false;
                // The backlight row only while the real backlight is in use; the rest closes up when it is not.
                // (Worked out from the other rows, so it holds at any display scale.)
                bool hw = s.HardwareBrightness;
                lightLabel.Visible = lightText.Visible = light.Visible = hw;
                int step = blueLabel.Top - brightLabel.Top;   // one row
                blinkLabel.Top = (hw ? lightLabel.Top : blueLabel.Top) + step * 56 / 50;
                blink.Top = blinkLabel.Top + Math.Max(1, step / 50);
                statusLabel.Top = blinkLabel.Top + step * 36 / 50;
                focusPause.Top = focusStop.Top = statusLabel.Top + (statusLabel.Height - focusPause.Height) / 2;
                ClientSize = new Size(ClientSize.Width, statusLabel.Top + step * 36 / 50);   // (the buttons beside it as far from the bottom as from the side)
                ShowValues();
            }

            // The line at the bottom: the time to the next break, or what the focus timer is at - with the focus timer's
            // buttons beside it: pause (or resume) and stop while it runs, and while it is off a play button to start it.
            // (None during its rest: the rest card is up then.)
            public void SetStatus(string text, int focus)
            {
                if (statusLabel.Text != text) statusLabel.Text = text;
                bool on = focus == App.FocusWorking || focus == App.FocusPaused;
                focusStop.Visible = on;
                focusPause.Visible = focus != App.FocusResting;
                focusPause.Left = on ? focusStop.Left - focusPause.Width - focusPause.Width / 5 : focusStop.Left;
                string glyph = focus == App.FocusWorking ? "" : "";   // pause : play
                if (focusPause.Text != glyph) focusPause.Text = glyph;
                string tip = Lang.T(focus == App.FocusWorking ? "Pause" : focus == App.FocusPaused ? "Resume" : "Start the focus timer");
                if (tip != shownTip) { shownTip = tip; tips.SetToolTip(focusPause, tip); }   // (only when it changes: this runs 8 times a second)
                statusLabel.Width = (focusPause.Visible ? focusPause.Left - statusLabel.Left / 3 : ClientSize.Width - statusLabel.Left) - statusLabel.Left;
            }

            void ShowValues()
            {
                lightText.Text = Math.Max(0, s.Backlight) + "%";
                brightText.Text = s.Brightness + "%";
                blueText.Text = s.BlueFilter == 0 ? Lang.T("Off") : ScreenFilter.KelvinFor(s.BlueFilter) + " K";
            }
        }
    }
}
