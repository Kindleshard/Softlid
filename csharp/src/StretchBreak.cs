using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace BlinkBuddy
{
    // A stand-up break with a coach: a vector stick figure on a dark full screen shows each stretch (the usual
    // low-risk desk-break routine), morphing smoothly from one to the next. It stands up from a seat at the start and
    // waves at the end; a spoken line (when on) introduces each stretch and is never cut. Esc skips; Snooze waits 5 min.
    class StretchBreak : Form
    {
        // Joints, in body units (y up, floor at 0, about 1.75 tall).
        const int Head = 0, Neck = 1, ShL = 2, ShR = 3, ElL = 4, ElR = 5, HaL = 6, HaR = 7, Pelvis = 8,
                  HipL = 9, HipR = 10, KnL = 11, KnR = 12, FtL = 13, FtR = 14, Toe = 15, ToeR = 16, Count = 17;
        const float HeadR = 0.12f;

        struct Stretch { public string Key, Title, Text; public Func<double, PointF[]> Pose; }

        readonly Stretch[] routine;
        readonly Stretch intro, outro;

        [DllImport("winmm.dll")] static extern int timeBeginPeriod(int ms);
        [DllImport("winmm.dll")] static extern int timeEndPeriod(int ms);

        public Action<int> Finished;   // receives the snooze length in seconds, or 0
        static readonly Color Back = Color.FromArgb(13, 15, 19);
        readonly Timer frame = new Timer { Interval = 15 };
        readonly Stopwatch clock = new Stopwatch();
        readonly bool sounds, forced;   // forced: no Skip, Snooze or Esc
        readonly Voice voice;
        readonly List<Stretch> plan = new List<Stretch>();
        readonly List<double> starts = new List<double>();
        readonly double total, lead, outroLen;
        readonly Font titleFont, textFont, smallFont;
        readonly float scale;
        PointF[] pose, from;
        int current = -2, spoken = -1, shownSeconds = -1, snooze;
        double blendStart, outroAt = -1, closeAt = -1;
        Rectangle lastBox;

        public StretchBreak(Settings s)
        {
            routine = new[]
            {
                new Stretch { Key = "stretch_reach", Title = "Overhead reach", Text = "Reach both arms up high, and stretch tall.", Pose = Reach },
                new Stretch { Key = "stretch_shoulders", Title = "Shoulder rolls", Text = "Roll your shoulders slowly backwards.", Pose = Shoulders },
                new Stretch { Key = "stretch_neck", Title = "Neck stretch", Text = "Gently tilt your head toward each shoulder.", Pose = NeckTilt },
                new Stretch { Key = "stretch_twist", Title = "Torso twist", Text = "Twist your upper body slowly, side to side.", Pose = Twist },
                new Stretch { Key = "stretch_chest", Title = "Chest opener", Text = "Open your arms wide, and squeeze your shoulder blades together.", Pose = Chest },
                new Stretch { Key = "stretch_march", Title = "March in place", Text = "March in place, lifting your knees.", Pose = March },
                new Stretch { Key = "stretch_calves", Title = "Calf raises", Text = "Rise up onto your toes, then lower slowly.", Pose = Calves },
                new Stretch { Key = "stretch_fold", Title = "Forward bend", Text = "Bend your knees a little, and hang forward to stretch your back.", Pose = Fold }
            };
            intro = new Stretch { Key = "stretch_intro", Title = "Time to stand up", Text = "Time to stand up. Follow along with a few easy stretches.", Pose = StandUp };
            outro = new Stretch { Key = "stretch_done", Title = "Great job!", Text = "Great job. Have a sip of water, and back to it.", Pose = Wave };

            sounds = s.Sounds;
            forced = s.ForceRest;
            voice = s.StretchVoice ? new Voice() : null;
            total = s.LongBreakDuration * 60;
            lead = sounds ? 1.0 : 0.3;
            double introLen = Math.Max(4.0, Voice.Length(intro.Key) + Voice.Pause + 1.0);
            outroLen = Math.Max(3.0, Voice.Length(outro.Key) + 0.8);
            int count = Math.Max(2, Math.Min(16, (int)Math.Round(total / 38)));
            double seg = Math.Max(15, (total - lead - introLen - outroLen) / count);
            plan.Add(intro);
            starts.Add(0);
            double t = lead + introLen;
            for (int i = 0; i < count; i++)
            {
                plan.Add(routine[i % routine.Length]);
                starts.Add(t);
                t += seg;
            }
            starts.Add(t);   // where the last one ends

            FormBorderStyle = FormBorderStyle.None;
            ShowInTaskbar = false;
            StartPosition = FormStartPosition.Manual;
            TopMost = true;
            BackColor = Back;
            DoubleBuffered = true;
            KeyPreview = true;
            Opacity = 0;
            Bounds = Screen.FromPoint(Cursor.Position).Bounds;
            using (Graphics g = Graphics.FromHwnd(IntPtr.Zero)) scale = g.DpiX / 96f;
            titleFont = new Font(Lang.Family == "Segoe UI" ? "Segoe UI Light" : Lang.Family, 30f);
            textFont = new Font(Lang.Family, 13f);
            smallFont = new Font(Lang.Family, 10f);

            var skip = new RoundButton { Text = "Skip", BackColor = Back };
            var later = new RoundButton { Text = "Snooze 5 min", BackColor = Back };
            int h = (int)(34 * scale), m = (int)(28 * scale);
            skip.Bounds = new Rectangle(Width - m - (int)(88 * scale), Height - m - h, (int)(88 * scale), h);
            later.Bounds = new Rectangle(skip.Left - (int)(140 * scale), skip.Top, (int)(128 * scale), h);
            skip.Click += delegate { Finish(0); };
            later.Click += delegate { Finish(300); };
            Controls.Add(skip);
            Controls.Add(later);
            skip.Visible = later.Visible = !forced;
            pose = StandUp(0);
            frame.Tick += delegate { Frame(); };
        }

        protected override void OnShown(EventArgs e)
        {
            base.OnShown(e);
            Activate();
            timeBeginPeriod(1);
            if (sounds) Chime.Play(false);
            clock.Start();
            frame.Start();
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            base.OnKeyDown(e);
            if (e.KeyCode == Keys.Escape && !forced) Finish(0);
        }

        void Finish(int seconds)
        {
            if (closeAt >= 0) return;
            snooze = seconds;
            if (voice != null) voice.Stop();
            closeAt = clock.Elapsed.TotalSeconds;
        }

        void Frame()
        {
            double t = clock.Elapsed.TotalSeconds;
            if (closeAt >= 0)
            {
                double f = 1 - (t - closeAt) / 0.6;
                if (f <= 0) { Close(); return; }
                Opacity = f;
                return;
            }
            Opacity = Math.Min(1, t / 0.5);
            double end = starts[starts.Count - 1];
            if (t >= end)
            {
                // The goodbye: a wave and the closing line, played to its end, then the chime.
                if (outroAt < 0)
                {
                    outroAt = t;
                    from = pose;
                    blendStart = t;
                    if (voice != null) voice.Say(outro.Key, outro.Text);
                    current = plan.Count;
                    Invalidate();
                }
                pose = Blend(from, outro.Pose(t - outroAt), t - blendStart);
                if (t - outroAt >= outroLen)
                {
                    if (sounds) Chime.Play(true);
                    Finish(0);
                }
                Redraw();
                return;
            }
            int now = 0;
            while (now + 1 < plan.Count && t >= starts[now + 1]) now++;
            double u = t - starts[now];
            if (now != current)
            {
                current = now;
                from = pose;
                blendStart = t;
                Invalidate();   // titles change
            }
            if (voice != null && spoken != now && (now > 0 || t >= lead))
            {
                spoken = now;
                voice.Say(plan[now].Key, plan[now].Text);
            }
            int left = (int)Math.Ceiling(end + outroLen - t);
            if (left != shownSeconds)
            {
                shownSeconds = left;
                Invalidate(FooterBox());
            }
            pose = Blend(from, plan[now].Pose(now == 0 ? Math.Max(0, t - lead) : u), t - blendStart);
            Redraw();
        }

        // Morphs from the last pose into the new stretch over the first moments of it.
        static PointF[] Blend(PointF[] a, PointF[] b, double since)
        {
            if (a == null) return b;
            double k = Math.Max(0, Math.Min(1, since / 0.9));
            k = k * k * (3 - 2 * k);
            if (k >= 1) return b;
            var p = new PointF[Count];
            for (int i = 0; i < Count; i++) p[i] = new PointF((float)(a[i].X + (b[i].X - a[i].X) * k), (float)(a[i].Y + (b[i].Y - a[i].Y) * k));
            return p;
        }

        // ---- Poses (t: seconds into the stretch) ----

        static PointF P(double x, double y) { return new PointF((float)x, (float)y); }
        static double Wave01(double t, double period) { return 0.5 - 0.5 * Math.Cos(2 * Math.PI * t / period); }
        static double Ease(double x) { x = Math.Max(0, Math.Min(1, x)); return x * x * (3 - 2 * x); }

        static PointF[] Standing(double t)
        {
            float b = (float)(0.007 * Math.Sin(2 * Math.PI * t / 4));   // breathing
            var p = new PointF[Count];
            p[Pelvis] = P(0, 0.95);
            p[Neck] = P(0, 1.45 + b);
            p[Head] = P(0, 1.62 + b);
            p[ShL] = P(-0.19, 1.43 + b); p[ShR] = P(0.19, 1.43 + b);
            p[ElL] = P(-0.23, 1.17 + b); p[ElR] = P(0.23, 1.17 + b);
            p[HaL] = P(-0.25, 0.92 + b); p[HaR] = P(0.25, 0.92 + b);
            p[HipL] = P(-0.1, 0.95); p[HipR] = P(0.1, 0.95);
            p[KnL] = P(-0.11, 0.5); p[KnR] = P(0.11, 0.5);
            p[FtL] = P(-0.13, 0.05); p[FtR] = P(0.13, 0.05);
            p[Toe] = P(-0.2, 0.0); p[ToeR] = P(0.2, 0.0);
            return p;
        }

        static void Lift(PointF[] p, double dy, bool feetToo)
        {
            for (int i = 0; i < Count; i++)
                if (feetToo || (i != Toe && i != ToeR)) p[i] = P(p[i].X, p[i].Y + dy);
        }

        static void Lean(PointF[] p, double dx)   // shifts the upper body sideways, more the higher it is
        {
            foreach (int i in new[] { Head, Neck, ShL, ShR, ElL, ElR, HaL, HaR })
                p[i] = P(p[i].X + dx * (p[i].Y - 0.95) / 0.7, p[i].Y);
        }

        static PointF Mix(PointF a, PointF b, double k) { return P(a.X + (b.X - a.X) * k, a.Y + (b.Y - a.Y) * k); }

        // From sitting to standing, then breathing.
        static PointF[] StandUp(double t)
        {
            PointF[] p = Standing(t);
            double up = Ease((t - 0.6) / 1.6), down = (1 - up) * 0.36;
            if (down <= 0) return p;
            foreach (int i in new[] { Head, Neck, ShL, ShR, ElL, ElR, HaL, HaR, Pelvis, HipL, HipR }) p[i] = P(p[i].X, p[i].Y - down);
            // seated, the thighs point at us: the knees sit just below the hips and a little wider
            p[KnL] = Mix(p[KnL], P(-0.16, 0.52), 1 - up); p[KnR] = Mix(p[KnR], P(0.16, 0.52), 1 - up);
            p[HaL] = Mix(p[HaL], P(-0.17, 0.58), 1 - up); p[HaR] = Mix(p[HaR], P(0.17, 0.58), 1 - up);   // hands on the knees
            p[ElL] = Mix(p[ElL], P(-0.27, 0.8), 1 - up); p[ElR] = Mix(p[ElR], P(0.27, 0.8), 1 - up);
            return p;
        }

        static PointF[] Reach(double t)
        {
            PointF[] p = Standing(t);
            double c = Ease(Wave01(t, 6) * 1.25);
            p[ElL] = Mix(p[ElL], P(-0.22, 1.74), c); p[ElR] = Mix(p[ElR], P(0.22, 1.74), c);
            p[HaL] = Mix(p[HaL], P(-0.14, 2.02), c); p[HaR] = Mix(p[HaR], P(0.14, 2.02), c);
            Lean(p, 0.08 * c * Math.Sin(2 * Math.PI * t / 12));
            Lift(p, 0.035 * c, false);
            return p;
        }

        static PointF[] Shoulders(double t)
        {
            PointF[] p = Standing(t);
            double th = 2 * Math.PI * t / 2.6, dy = 0.04 * Math.Sin(th), dx = 0.022 * Math.Cos(th);
            p[ShL] = P(p[ShL].X - dx, p[ShL].Y + dy); p[ShR] = P(p[ShR].X + dx, p[ShR].Y + dy);
            p[ElL] = P(p[ShL].X - 0.04, p[ShL].Y - 0.26); p[ElR] = P(p[ShR].X + 0.04, p[ShR].Y - 0.26);
            p[HaL] = P(p[ElL].X - 0.02, p[ElL].Y - 0.25); p[HaR] = P(p[ElR].X + 0.02, p[ElR].Y - 0.25);
            return p;
        }

        static PointF[] NeckTilt(double t)
        {
            PointF[] p = Standing(t);
            double a = 0.5 * Math.Sin(2 * Math.PI * t / 7);
            p[Head] = P(p[Neck].X + 0.17 * Math.Sin(a), p[Neck].Y + 0.17 * Math.Cos(a));
            return p;
        }

        static PointF[] Twist(double t)
        {
            PointF[] p = Standing(t);
            double phi = 0.9 * Math.Sin(2 * Math.PI * t / 5), c = Math.Cos(phi), sn = Math.Sin(phi);
            p[ShL] = P(-0.19 * c, p[ShL].Y); p[ShR] = P(0.19 * c, p[ShR].Y);
            // forearms folded across the chest, turning with it
            p[HaL] = P(0.04 + 0.16 * sn, 1.3); p[HaR] = P(-0.04 + 0.16 * sn, 1.33);
            p[ElL] = P(p[ShL].X - 0.1 * c + 0.05 * sn, 1.22); p[ElR] = P(p[ShR].X + 0.1 * c + 0.05 * sn, 1.22);
            p[Head] = P(p[Head].X + 0.035 * sn, p[Head].Y);
            return p;
        }

        static PointF[] Chest(double t)
        {
            PointF[] p = Standing(t);
            double open = Ease(Wave01(t, 4.5) * 1.2);
            p[ElL] = Mix(P(-0.24, 1.3), P(-0.4, 1.42), open); p[ElR] = Mix(P(0.24, 1.3), P(0.4, 1.42), open);
            p[HaL] = Mix(P(-0.1, 1.38), P(-0.43, 1.7), open); p[HaR] = Mix(P(0.1, 1.38), P(0.43, 1.7), open);
            p[ShL] = P(p[ShL].X - 0.02 * open, p[ShL].Y); p[ShR] = P(p[ShR].X + 0.02 * open, p[ShR].Y);
            return p;
        }

        static PointF[] March(double t)
        {
            PointF[] p = Standing(t);
            double s = Math.Sin(2 * Math.PI * t / 1.5), l = Math.Max(0, s), r = Math.Max(0, -s);
            p[KnL] = P(-0.12, 0.5 + 0.26 * l); p[FtL] = P(-0.12, 0.05 + 0.3 * l); p[Toe] = P(-0.19, 0.0 + 0.3 * l);
            p[KnR] = P(0.12, 0.5 + 0.26 * r); p[FtR] = P(0.12, 0.05 + 0.3 * r); p[ToeR] = P(0.19, 0.0 + 0.3 * r);
            // arms swing with the opposite leg
            p[ElR] = P(0.24, 1.17 + 0.05 * l); p[HaR] = P(0.22, 0.92 + 0.2 * l);
            p[ElL] = P(-0.24, 1.17 + 0.05 * r); p[HaL] = P(-0.22, 0.92 + 0.2 * r);
            Lift(p, 0.015 * Math.Abs(s), true);
            return p;
        }

        static PointF[] Calves(double t)
        {
            PointF[] p = Standing(t);
            double up = Ease(Wave01(t, 3.2) * 1.2);
            p[ElL] = P(-0.31, 1.12); p[ElR] = P(0.31, 1.12);   // hands on hips
            p[HaL] = P(-0.14, 0.99); p[HaR] = P(0.14, 0.99);
            Lift(p, 0.075 * up, false);   // the toes stay down, the heels rise
            return p;
        }

        // Seen from the side: hinge forward at the hips, arms hanging, knees soft.
        static PointF[] Fold(double t)
        {
            double bend = Ease(Wave01(t, 7) * 1.15), a = bend * 75 * Math.PI / 180;
            var p = new PointF[Count];
            PointF pelvis = P(-0.06 * bend, 0.95 - 0.03 * bend);
            p[Pelvis] = pelvis; p[HipL] = pelvis; p[HipR] = pelvis;
            p[Neck] = P(pelvis.X + 0.5 * Math.Sin(a), pelvis.Y + 0.5 * Math.Cos(a));
            p[Head] = P(p[Neck].X + 0.17 * Math.Sin(a + 0.15 * bend), p[Neck].Y + 0.17 * Math.Cos(a + 0.15 * bend));
            p[ShL] = P(p[Neck].X - 0.02, p[Neck].Y - 0.02); p[ShR] = p[ShL];
            p[ElL] = P(p[ShL].X + 0.02, p[ShL].Y - 0.27); p[ElR] = P(p[ShL].X + 0.05, p[ShL].Y - 0.27);
            p[HaL] = P(p[ElL].X + 0.03, p[ElL].Y - 0.26); p[HaR] = P(p[ElR].X + 0.03, p[ElR].Y - 0.26);
            p[KnL] = P(0.05 + 0.04 * bend, 0.5); p[KnR] = P(0.08 + 0.04 * bend, 0.5);
            p[FtL] = P(-0.02, 0.05); p[FtR] = P(0.01, 0.05);
            p[Toe] = P(0.1, 0.0); p[ToeR] = P(0.13, 0.0);
            return p;
        }

        static PointF[] Wave(double t)
        {
            PointF[] p = Standing(t);
            double w = Math.Sin(2 * Math.PI * t / 0.9);
            p[ElR] = P(0.36, 1.45); p[HaR] = P(0.42 + 0.06 * w, 1.75);
            return p;
        }

        // ---- Drawing ----

        float K { get { return Height * 0.44f / 2.05f; } }               // body units to pixels
        PointF Floor { get { return new PointF(Width / 2f, Height * 0.66f); } }
        PointF ToPx(PointF b) { return new PointF(Floor.X + b.X * K, Floor.Y - b.Y * K); }

        Rectangle FigureBox()
        {
            float k = K;
            return Rectangle.Round(RectangleF.FromLTRB(Floor.X - 0.75f * k, Floor.Y - 2.25f * k, Floor.X + 0.75f * k, Floor.Y + 0.15f * k));
        }

        Rectangle FooterBox() { return new Rectangle(0, Height - (int)(120 * scale), Width - (int)(300 * scale), (int)(120 * scale)); }

        void Redraw()
        {
            Rectangle box = FigureBox();
            Invalidate(lastBox.IsEmpty ? box : Rectangle.Union(lastBox, box));
            lastBox = box;
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.Clear(Back);
            g.SmoothingMode = SmoothingMode.AntiAlias;
            DrawShadow(g);
            DrawFigure(g);
            DrawTitles(g);
            DrawFooter(g);
        }

        void DrawShadow(Graphics g)
        {
            float k = K, w = 0.8f * k, h = 0.1f * k;
            using (var path = new GraphicsPath())
            {
                path.AddEllipse(Floor.X - w / 2, Floor.Y - h / 2, w, h);
                using (var b = new PathGradientBrush(path) { CenterColor = Color.FromArgb(70, Theme.Accent), SurroundColors = new[] { Color.FromArgb(0, Theme.Accent) } })
                    g.FillPath(b, path);
            }
        }

        void DrawFigure(Graphics g)
        {
            if (pose == null) return;
            float k = K, w = Math.Max(3f, k * 0.05f);
            int[][] limbs =
            {
                new[] { Neck, Pelvis }, new[] { ShL, ShR }, new[] { HipL, HipR },
                new[] { ShL, ElL, HaL }, new[] { ShR, ElR, HaR },
                new[] { HipL, KnL, FtL, Toe }, new[] { HipR, KnR, FtR, ToeR }
            };
            // a soft glow underneath, then the limbs
            foreach (var pass in new[] { new { Width = w * 3.2f, Color = Color.FromArgb(38, Theme.Accent) }, new { Width = w, Color = Color.FromArgb(232, 238, 246) } })
                using (var pen = new Pen(pass.Color, pass.Width) { StartCap = LineCap.Round, EndCap = LineCap.Round, LineJoin = LineJoin.Round })
                {
                    foreach (int[] limb in limbs)
                    {
                        var pts = new PointF[limb.Length];
                        for (int i = 0; i < limb.Length; i++) pts[i] = ToPx(pose[limb[i]]);
                        g.DrawLines(pen, pts);
                    }
                    PointF neck = ToPx(pose[Neck]), head = ToPx(pose[Head]);
                    g.DrawLine(pen, neck, Mix(neck, head, 0.4));
                    float r = HeadR * k;
                    using (var fill = new SolidBrush(Back)) g.FillEllipse(fill, head.X - r, head.Y - r, 2 * r, 2 * r);
                    g.DrawEllipse(pen, head.X - r, head.Y - r, 2 * r, 2 * r);
                }
            // hands and joints as small dots, for a little life
            using (var dot = new SolidBrush(Theme.Accent))
                foreach (int j in new[] { HaL, HaR })
                {
                    PointF h = ToPx(pose[j]);
                    g.FillEllipse(dot, h.X - w * 0.9f, h.Y - w * 0.9f, w * 1.8f, w * 1.8f);
                }
        }

        void DrawTitles(Graphics g)
        {
            Stretch now = current >= plan.Count ? outro : current < 0 ? intro : plan[current];
            var centred = new StringFormat { Alignment = StringAlignment.Center };
            float y = Floor.Y + K * 0.25f;
            using (var b = new SolidBrush(Color.FromArgb(232, 236, 242)))
                g.DrawString(Lang.T(now.Title), titleFont, b, new RectangleF(0, y, Width, titleFont.Height * 1.3f), centred);
            using (var b = new SolidBrush(Color.FromArgb(138, 145, 156)))
                g.DrawString(Lang.T(now.Text), textFont, b, new RectangleF(Width * 0.2f, y + titleFont.Height * 1.25f, Width * 0.6f, textFont.Height * 2.4f), centred);
        }

        void DrawFooter(Graphics g)
        {
            Rectangle box = FooterBox();
            float m = 28 * scale, barY = Height - m - 17 * scale, barW = Width * 0.32f;
            double end = starts[starts.Count - 1] + outroLen, t = clock.Elapsed.TotalSeconds;
            float done = (float)Math.Max(0, Math.Min(1, t / end));
            using (var track = new Pen(Color.FromArgb(48, 52, 60), 4 * scale) { StartCap = LineCap.Round, EndCap = LineCap.Round })
                g.DrawLine(track, m, barY, m + barW, barY);
            if (done > 0)
                using (var fill = new Pen(Theme.Accent, 4 * scale) { StartCap = LineCap.Round, EndCap = LineCap.Round })
                    g.DrawLine(fill, m, barY, m + barW * done, barY);
            string next = current >= 0 && current + 1 < plan.Count ? Lang.F("Next: {0}", Lang.T(plan[current + 1].Title)) + "  ·  " : "";
            string time = forced ? string.Format("{0}:{1:00}", Math.Max(0, shownSeconds) / 60, Math.Max(0, shownSeconds) % 60)
                : Lang.F("{0}:{1:00} left  ·  Esc to skip", Math.Max(0, shownSeconds) / 60, Math.Max(0, shownSeconds) % 60);
            using (var b = new SolidBrush(Color.FromArgb(110, 116, 126)))
                g.DrawString(next + time, smallFont, b, m, barY - smallFont.Height * 1.8f);
        }

        protected override void OnFormClosed(FormClosedEventArgs e)
        {
            frame.Dispose();
            timeEndPeriod(1);
            if (voice != null) voice.Dispose();
            titleFont.Dispose();
            textFont.Dispose();
            smallFont.Dispose();
            base.OnFormClosed(e);
            if (Finished != null) Finished(snooze);
        }
    }
}
