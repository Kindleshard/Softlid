using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace BlinkBuddy
{
    // A look-away break with eye exercises, on the whole screen: it fades to black (nothing left to look at), then a
    // drop of water with an eye in it fades in, looking at you, and acts out each exercise like a cartoon liquid:
    // - left and right: it glances the way it will go, then shoots across, slaps against the side and sticks; to
    //   leave it has to pull free, stretching on a sticky neck until it snaps loose and whips across to the other side
    // - up and down: it falls to the floor and splats, then gets sucked up to the ceiling on a long thread, hangs
    //   there a moment and drips down again
    // - the circle: it slides round the screen leaving a glossy trail behind it like a snail, which beads up and dries
    // - closing: it turns into a real eye, with lids that close and open
    // Each movement takes at most 3 s; with more time it repeats (twice, then with a rest after it, then more often).
    // Spoken lines (when on) are never cut: the steps are timed around them. Esc skips.
    class GuidedBreak : Form
    {
        struct Step { public string Key, Text; }
        static readonly Step[] Steps =
        {
            new Step { Key = "far", Text = "Look away from the screen, at something far away, and let your eyes soften." },
            new Step { Key = "sides", Text = "Slowly look left and right, keeping your head still." },
            new Step { Key = "updown", Text = "Now slowly look up and down." },
            new Step { Key = "circle", Text = "Roll your eyes in a slow, wide circle." },
            new Step { Key = "close", Text = "Close your eyes gently, then blink slowly a few times." }
        };
        static readonly double[] StepWeight = { 0.5, 1, 1, 1, 1 };   // looking far away needs less time than the rest
        const string DoneKey = "done", DoneText = "Well done. You can look back now.";
        const double FadeIn = 0.5, FadeOut = 0.9;
        // The tail: three smaller blobs, each on a soft spring behind the one before.
        static readonly double[] TailR = { 0.68, 0.52, 0.38 }, TailM = { 0.95, 0.85, 0.75 }, TailK = { 520, 430, 350 };
        // The snail trail: little blobs dropped along the way, drying up over TrailLife seconds.
        const int TrailMax = 240, Body = 1 + 3;
        const double TrailLife = 1.1;

        [DllImport("winmm.dll")] static extern int timeBeginPeriod(int ms);
        [DllImport("winmm.dll")] static extern int timeEndPeriod(int ms);

        public Action<int> Finished;   // receives the snooze length in seconds (always 0 here)
        readonly Timer frame = new Timer { Interval = 15 };
        readonly Stopwatch clock = new Stopwatch();
        readonly List<Shade> others = new List<Shade>();
        readonly bool sounds, forced;   // forced: Esc doesn't skip
        readonly Voice voice;
        readonly SessionPlan plan;
        readonly Font captionFont, hintFont;
        readonly Liquid liquid = new Liquid(Body + TrailMax);
        readonly double[] trailBorn = new double[TrailMax];
        readonly bool backdrop;        // a black background (off: the exercises play over the screen)
        Layer drop, caption;           // see-through windows above the break: the liquid, and the words
        Bitmap captionArt;
        bool liquidShown;
        double fadeK;                  // the fade in and out, 0..1
        bool started;

        // How warm the water looks: the blue light filter in effect when the break starts (0..1).
        public float Warmth { set { liquid.Warm = value; } }
        int step = -2, spoken = -1, shownSeconds = -1, trailNext;
        double outroAt = -1, fadeAt = -1, lastT, trailX, trailY;
        float alpha;

        // What the current moment asks of the drop.
        double px, py, mainM, grow, flatX, flatY, tail, stiff, eyeShape, open, lookTX, lookTY;
        int axis;                          // stretch along x (0), y (1), the way it moves (2), or not at all (-1)
        double anchorX, anchorY, stick;    // a point on a wall the tail is still stuck to, and how firmly
        bool trailOn;
        // Its own momentum: a stretch that lags behind the speed, and eyes that swing to where they look.
        double prevX, prevY, stretch = 1, stretchSpeed, lookX, lookY, lookVX, lookVY;

        public GuidedBreak(Settings s)
        {
            sounds = s.Sounds;
            forced = s.ForceRest;
            backdrop = s.BreakBackdrop;
            int needed = Voice.MinimumSeconds(Voice.LookAwayKeys);
            // Voice only when every line fits; otherwise a line would have to be cut.
            bool voiced = s.ExerciseVoice && (needed == 0 || s.BreakDuration >= needed);
            voice = voiced ? new Voice() : null;
            var keys = new string[Steps.Length];
            for (int i = 0; i < keys.Length; i++) keys[i] = Steps[i].Key;
            plan = new SessionPlan(keys, DoneKey, s.BreakDuration, 2.5, sounds ? 1.0 : 0.3, voiced && needed > 0, StepWeight);
            FormBorderStyle = FormBorderStyle.None;
            ShowInTaskbar = false;
            StartPosition = FormStartPosition.Manual;
            TopMost = true;
            BackColor = Color.Black;
            DoubleBuffered = true;
            KeyPreview = true;
            Opacity = 0;
            Bounds = Screen.FromPoint(Cursor.Position).Bounds;
            captionFont = new Font(Lang.Family, 14f);
            hintFont = new Font(Lang.Family, 10f);
            for (int i = 0; i < TrailMax; i++) trailBorn[i] = -100;
            frame.Tick += delegate { Frame(); };
        }

        protected override void OnShown(EventArgs e)
        {
            base.OnShown(e);
            if (backdrop)
                foreach (Screen sc in Screen.AllScreens)
                {
                    if (sc.Bounds == Bounds) continue;
                    var shade = new Shade(sc.Bounds, Color.Black, 0.01);
                    others.Add(shade);
                    shade.Show();
                }
            // The liquid and the words float above, in windows of their own (owned, so always above this one).
            drop = new Layer(Bounds) { Owner = this };
            caption = new Layer(Bounds) { Owner = this };
            drop.Show();
            caption.Show();
            Activate();
            timeBeginPeriod(1);   // even frame times for the length of the break
            if (sounds) Chime.Play(false);
            clock.Start();
            frame.Start();
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            base.OnKeyDown(e);
            if (e.KeyCode != Keys.Escape || fadeAt >= 0 || forced) return;
            if (voice != null) voice.Stop();
            fadeAt = clock.Elapsed.TotalSeconds;
        }

        void Frame()
        {
            double t = clock.Elapsed.TotalSeconds, dt = Math.Max(0.001, Math.Min(0.05, t - lastT));
            lastT = t;
            if (fadeAt >= 0)
            {
                double f = 1 - (t - fadeAt) / FadeOut;
                if (f <= 0) { Close(); return; }
                SetFade(f);
                return;
            }
            SetFade(Math.Min(1, t / FadeIn));
            double run = t - FadeIn;   // time into the exercises

            // After the last step: the drop fades away, the closing line plays to its end, then the chime, then the
            // screen comes back.
            if (run >= plan.End)
            {
                if (outroAt < 0)
                {
                    outroAt = t;
                    if (voice != null) voice.Say(DoneKey, DoneText);
                    UpdateCaption();
                }
                Animate(Steps.Length, t - outroAt, 1, dt, t);
                Redraw();
                double wait = voice != null ? Math.Max(0.9, Voice.Length(DoneKey) + 0.25) : 0.9;
                if (t - outroAt >= wait)
                {
                    if (sounds) Chime.Play(true);
                    fadeAt = t;
                }
                return;
            }

            double u;
            int now = plan.At(run, out u);
            if (now != step)
            {
                step = now;
                UpdateCaption();
            }
            if (voice != null && step >= 0 && spoken != step && (step > 0 || u >= plan.Lead))
            {
                spoken = step;
                voice.Say(Steps[step].Key, Steps[step].Text);
            }
            int left = (int)Math.Ceiling(plan.End - Math.Max(0, run));
            if (left != shownSeconds)
            {
                shownSeconds = left;
                UpdateCaption();
            }
            Animate(step, u, step >= 0 ? plan.Length[step] : 1, dt, t);
            Redraw();
        }

        // The fade in and out: the background (black, or almost nothing when the break plays over the screen; it
        // still takes the keyboard for Esc), the other screens' shades, and the liquid and words above.
        void SetFade(double f)
        {
            double eased = f * f * (3 - 2 * f);
            if (Math.Abs(eased - fadeK) < 0.001 && fadeK > 0) return;
            fadeK = eased;
            Opacity = backdrop ? eased : 0.01;
            foreach (Shade sh in others) sh.Opacity = Math.Min(0.95, eased);
            ShowCaption();
            if (liquidShown) liquid.Present(drop.Handle, Location, (byte)(255 * fadeK));
        }

        // ---- Timing ----

        // How an exercise spends its step: a glance (and any setting off), the movement repeated, the way back to
        // the middle, then a rest until the next exercise. A movement takes up to 3 s; with more time it is done
        // twice, then twice with a rest of up to 4 s after it, then three or more times.
        struct Timing { public double Pre, Cycle, Moves, Back; public int Reps; }

        static Timing Plan(double len, double pre, double back)
        {
            var tm = new Timing { Pre = pre, Back = back };
            double a = Math.Max(0.8, len - pre - back);
            if (a <= 3) { tm.Reps = 1; tm.Cycle = a; }
            else if (a <= 6) { tm.Reps = 2; tm.Cycle = a / 2; }
            else if (a <= 10) { tm.Reps = 2; tm.Cycle = 3; }
            else
            {
                tm.Reps = Math.Max(3, (int)Math.Ceiling((a - 4) / 3));
                tm.Cycle = (a - 4) / tm.Reps;
            }
            tm.Moves = tm.Reps * tm.Cycle;
            return tm;
        }

        // ---- Motion ----

        double Radius { get { return Math.Min(Width, Height) * 0.075; } }
        double CX { get { return Width / 2.0; } }
        double CY { get { return Height / 2.0; } }

        static double Smooth(double x) { x = Math.Max(0, Math.Min(1, x)); return x * x * (3 - 2 * x); }
        static double Smoother(double x) { x = Math.Max(0, Math.Min(1, x)); return x * x * x * (x * (6 * x - 15) + 10); }
        static double Pop(double x)   // ease out with an overshoot: liquid settling
        {
            x = Math.Max(0, Math.Min(1, x));
            const double c = 1.9;
            return 1 + (c + 1) * Math.Pow(x - 1, 3) + c * Math.Pow(x - 1, 2);
        }
        // A wobble dying away: how hard it is still squashed, ti seconds after a hit.
        static double Jiggle(double ti, double hold, double hit) { return hold + hit * Math.Exp(-ti * 6) * Math.Cos(ti * 21); }
        // Even speed with eased ends: a fraction ramp of the time to get going, the same to stop.
        static double Cruise(double x, double ramp)
        {
            x = Math.Max(0, Math.Min(1, x));
            double k = 1 / (1 - ramp);
            if (x < ramp) return k * x * x / (2 * ramp);
            if (x > 1 - ramp) return 1 - k * (1 - x) * (1 - x) / (2 * ramp);
            return k * (x - ramp / 2);
        }

        void Animate(int k, double u, double len, double dt, double t)
        {
            // By default: resting in the middle, looking at you, breathing.
            px = CX;
            py = CY;
            grow = 1 + 0.022 * Math.Sin(t * 2.1);
            mainM = flatX = flatY = tail = stiff = open = 1;
            stick = eyeShape = lookTX = lookTY = 0;
            axis = -1;
            alpha = 1;
            trailOn = false;
            foreach (Liquid.Film f in liquid.Films) f.A = 0;
            if (k < 0) { alpha = 0; return; }
            if (k == 0) Appear(u);
            else if (k == 1) Sides(u, len);
            else if (k == 2) UpDown(u, len);
            else if (k == 3) Circle(u, len);
            else if (k == 4) Blink(u, len);
            else
            {
                double g = Smooth((u - 0.1) / 0.7);   // the end: it shrinks away
                alpha = (float)(1 - g);
                grow *= 1 - 0.5 * g;
            }

            Liquid.Blob drop = liquid.Blobs[0];
            double R = Radius;
            if (!started)
            {
                started = true;
                prevX = trailX = px;
                prevY = trailY = py;
                for (int j = 1; j < Body; j++) { liquid.Blobs[j].X = px; liquid.Blobs[j].Y = py; }
            }

            // Viscosity: the stretch chases the speed through a soft spring, so it lags, overshoots and wobbles.
            double vx = (px - prevX) / dt, vy = (py - prevY) / dt, speed = Math.Sqrt(vx * vx + vy * vy);
            double target = 1 + Math.Min(0.6, speed / (Math.Max(Width, Height) * 0.7));
            stretchSpeed += (170 * (target - stretch) - 9 * stretchSpeed) * dt;
            stretch += stretchSpeed * dt;
            double along = stretch, across = 1 / Math.Sqrt(stretch), sx = 1, sy = 1;
            if (axis == 0) { sx = along; sy = across; }
            else if (axis == 1) { sx = across; sy = along; }
            else if (axis == 2 && speed > 1)
            {
                double c2 = vx * vx / (speed * speed);
                sx = across + (along - across) * c2;
                sy = along + (across - along) * c2;
            }
            drop.R = R * grow;
            drop.M = mainM;
            drop.SX = sx * flatX * (1 + 0.22 * eyeShape);
            drop.SY = sy * flatY * (1 - 0.14 * eyeShape);

            // The tail: each blob springs towards the one ahead of it (or, while still stuck, towards the wall).
            int n = Math.Max(1, (int)Math.Ceiling(dt * 240));
            double h = dt / n;
            for (int s = 1; s <= n; s++)
            {
                drop.X = prevX + (px - prevX) * s / n;
                drop.Y = prevY + (py - prevY) * s / n;
                for (int j = 1; j < Body; j++)
                {
                    Liquid.Blob b = liquid.Blobs[j], ahead = liquid.Blobs[j - 1];
                    double hold = Math.Max(0, Math.Min(1, stick * (0.6 + 0.4 * j / 3.0)));
                    double gx = ahead.X + (anchorX - ahead.X) * hold, gy = ahead.Y + (anchorY - ahead.Y) * hold;
                    double kk = TailK[j - 1] * stiff, damp = 2 * Math.Sqrt(kk) * 0.4;
                    b.VX += (kk * (gx - b.X) - damp * b.VX) * h;
                    b.VY += (kk * (gy - b.Y) - damp * b.VY) * h;
                    b.X += b.VX * h;
                    b.Y += b.VY * h;
                    // Surface tension: a blob never gets further from the one ahead than the liquid can stretch
                    // (much further while it is stuck to a wall, which is the sticky neck), so the tail stays one piece.
                    double ex = b.X - ahead.X, ey = b.Y - ahead.Y, d = Math.Sqrt(ex * ex + ey * ey), most = R * (0.8 + 2.6 * stick);
                    if (d > most) { b.X = ahead.X + ex * most / d; b.Y = ahead.Y + ey * most / d; }
                }
            }
            for (int j = 1; j < Body; j++)
            {
                liquid.Blobs[j].R = R * TailR[j - 1] * grow;
                liquid.Blobs[j].M = TailM[j - 1] * mainM * tail * (1 - eyeShape);
            }

            // The eyes swing to where they look, a little past it, and settle: quick, but alive.
            lookVX += (300 * (lookTX - lookX) - 16 * lookVX) * dt;
            lookVY += (300 * (lookTY - lookY) - 16 * lookVY) * dt;
            lookX += lookVX * dt;
            lookY += lookVY * dt;
            liquid.LookX = lookX;
            liquid.LookY = lookY;
            liquid.EyeShape = eyeShape;
            liquid.Open = open;
            Trail(t, R);
            prevX = px;
            prevY = py;
        }

        // The snail trail: while it is on, a small blob is left every little way; each dries up over its life,
        // thinning until the line beads up into droplets and is gone.
        void Trail(double t, double R)
        {
            double gap = R * 0.13;
            if (trailOn)
            {
                double dx = px - trailX, dy = py - trailY, d = Math.Sqrt(dx * dx + dy * dy);
                for (double s = gap; s <= d; s += gap)
                {
                    trailBorn[trailNext] = t;
                    liquid.Blobs[Body + trailNext].X = trailX + dx * s / d;
                    liquid.Blobs[Body + trailNext].Y = trailY + dy * s / d;
                    trailNext = (trailNext + 1) % TrailMax;
                    if (s + gap > d) { trailX += dx * s / d; trailY += dy * s / d; }
                }
            }
            else { trailX = px; trailY = py; }
            for (int i = 0; i < TrailMax; i++)
            {
                Liquid.Blob b = liquid.Blobs[Body + i];
                double life = 1 - (t - trailBorn[i]) / TrailLife;
                if (life <= 0) { b.M = 0; continue; }
                b.M = 0.85 * Math.Pow(life, 0.5) * alpha;
                b.R = R * 0.17 * (0.5 + 0.5 * life);
            }
        }

        // The first time: it fades in, small and growing, and looks at you.
        void Appear(double u)
        {
            alpha = (float)Smooth((u - 0.05) / 1.5);
            grow *= 0.35 + 0.65 * Pop((u - 0.05) / 1.3);
        }

        // Looking the way it is about to go, leaning that way a touch.
        void Glance(double u, double lx, double ly)
        {
            if (u < 0.06) return;
            lookTX = lx;
            lookTY = ly;
            double lean = Radius * 0.12 * Smooth((u - 0.06) / 0.3);
            px += lx * lean;
            py += ly * lean;
        }

        static int EdgeOf(bool sideways, int side) { return sideways ? (side < 0 ? 0 : 1) : (side < 0 ? 2 : 3); }

        void SetFilm(Liquid.Film f, int edge, double at, double amount, double spread)
        {
            f.Edge = edge;
            f.At = at;
            f.A = Math.Max(0, Math.Min(1, amount));
            f.Spread = spread;
            f.Thick = Radius * 0.95;
        }

        // Left and right: a glance to the left, then side to side (a movement is there and back), then back home.
        void Sides(double u, double len)
        {
            Timing tm = Plan(len, 0.45, 0.8);
            if (u < tm.Pre) { Glance(u, -1, 0); return; }
            double s = u - tm.Pre, leg = tm.Cycle / 2;
            if (s < tm.Moves)
            {
                int i = Math.Min(2 * tm.Reps - 1, (int)(s / leg));
                WallLeg(i == 0, i % 2 == 0 ? -1 : 1, (s - i * leg) / leg, leg);
                return;
            }
            s -= tm.Moves;
            if (s < tm.Back) BackFromSide(1, s / tm.Back, tm.Back);
        }

        // One crossing to the wall at side dir: pull free of the wall it is stuck to (or, the first time, draw back
        // to gather itself), shoot across, get grabbed by the far wall and slap into it, stuck there with a wobble.
        void WallLeg(bool fromMiddle, int dir, double v, double dur)
        {
            double R = Radius, W = Width, from = dir < 0 ? W : 0, to = dir < 0 ? 0 : W;
            double stuckFrom = from + dir * R * 0.8, stuckTo = to - dir * R * 0.8, approach = to - dir * R * 2.5;
            const double P1 = 0.3, P2 = 0.6, P3 = 0.68;
            double start = fromMiddle ? CX - dir * R * 0.45 : from + dir * R * 2.1, p, sq = 0;
            axis = 0;
            lookTX = dir;
            if (v < P1)
            {
                double x = v / P1;
                if (fromMiddle)
                {
                    p = CX - dir * R * 0.45 * Smooth(x);   // anticipation: drawing back before the jump
                    sq = 0.16 * Smooth(x);
                }
                else
                {
                    // Sticky: the body pulls away but the tail stays glued to the wall, stretching, then lets go.
                    p = stuckFrom + (start - stuckFrom) * Smooth(x);
                    stick = 1 - Smooth((x - 0.75) / 0.25);
                    anchorX = from;
                    anchorY = CY;
                    SetFilm(liquid.Films[0], EdgeOf(true, -dir), CY, 1 - Smooth((x - 0.1) / 0.9), R * (0.9 + 1.1 * (1 - Smooth(x))));
                    sq = -0.12 * Smooth(x);   // stretched thin by the pull
                }
            }
            else if (v < P2) p = start + (approach - start) * Smoother((v - P1) / (P2 - P1));
            else if (v < P3)
            {
                double x = (v - P2) / (P3 - P2);   // surface tension takes over: it accelerates into the wall
                p = approach + (stuckTo - approach) * x * x * x;
            }
            else
            {
                p = stuckTo;
                sq = Jiggle((v - P3) * dur, 0.22, 0.34);
            }
            if (v >= P2 - 0.05)
                SetFilm(liquid.Films[1], EdgeOf(true, dir), CY, Smooth((v - P2 + 0.05) / 0.1), R * (0.9 + 1.1 * Pop((v - P3) / 0.3)));
            px = p;
            py = CY;
            flatX = 1 - sq;
            flatY = 1 + sq * 0.7;
        }

        // From stuck on the wall at side, pulling free and gliding home, settling with a wobble.
        void BackFromSide(int side, double v, double dur)
        {
            double R = Radius, wall = side < 0 ? 0 : Width, stuck = wall - side * R * 0.8, pulled = wall - side * R * 1.9;
            axis = 0;
            if (v < 0.35)
            {
                double x = v / 0.35;
                px = stuck + (pulled - stuck) * Smooth(x);
                stick = 1 - Smooth((x - 0.7) / 0.3);
                anchorX = wall;
                anchorY = CY;
                SetFilm(liquid.Films[0], EdgeOf(true, side), CY, 1 - Smooth((x - 0.1) / 0.9), R * (0.9 + 1.1 * (1 - Smooth(x))));
                flatX = 1 + 0.12 * Smooth(x);
                lookTX = -side;
            }
            else
            {
                px = pulled + (CX - pulled) * Pop((v - 0.35) / 0.65);
                lookTX = v < 0.7 ? -side : 0;
            }
            py = CY;
        }

        // Up and down: a glance down, then fall and get sucked back up (a movement), then drip back home.
        void UpDown(double u, double len)
        {
            Timing tm = Plan(len, 0.45, 0.9);
            if (u < tm.Pre) { Glance(u, 0, 1); return; }
            double s = u - tm.Pre;
            if (s < tm.Moves)
            {
                int i = Math.Min(tm.Reps - 1, (int)(s / tm.Cycle));
                FallAndRise(i == 0, (s - i * tm.Cycle) / tm.Cycle, tm.Cycle);
                return;
            }
            s -= tm.Moves;
            if (s < tm.Back) DripHome(s / tm.Back);
        }

        // Falling (from the middle the first time, after that from the ceiling) under gravity, splatting on the
        // floor, then sucked up to the ceiling with a thread still tied to the floor until it snaps, and hanging there.
        void FallAndRise(bool first, double w, double c)
        {
            double R = Radius, floorY = Height - R * 0.8, ceilY = R * 0.8;
            Liquid.Film floor = liquid.Films[0], ceiling = liquid.Films[1];
            const double F = 0.36, S = 0.52, U = 0.86;
            double y, sq = 0;
            axis = 1;
            if (w < F)
            {
                double x = w / F, y0 = first ? CY : ceilY;
                y = y0 + (floorY - y0) * x * x;   // gravity
                lookTY = 1;
                if (!first)
                {
                    stick = 1 - Smooth((x - 0.2) / 0.25);   // a drip: it hangs on by a neck, then lets go
                    anchorX = CX;
                    anchorY = 0;
                    SetFilm(ceiling, 2, CX, 1 - Smooth(x / 0.6), R * (0.9 + 1.1 * (1 - Smooth(x / 0.6))));
                }
            }
            else if (w < S)
            {
                y = floorY;
                sq = Jiggle((w - F) * c, 0.24, 0.4);
                lookTY = w < F + 0.6 * (S - F) ? 1 : -1;   // then it looks up: something is pulling
            }
            else if (w < U)
            {
                double x = (w - S) / (U - S);
                y = floorY + (ceilY - floorY) * Math.Pow(x, 2.2);   // sucked up, faster and faster
                stick = 1 - Smooth((x - 0.3) / 0.3);                 // a long thread back to the floor, then it snaps
                anchorX = CX;
                anchorY = Height;
                SetFilm(floor, 3, CX, 1 - Smooth((x - 0.05) / 0.55), R * (0.9 + 1.4 * (1 - Smooth(x / 0.6))));
                lookTY = -1;
                sq = -0.1 * Smooth(x / 0.4);
            }
            else
            {
                y = ceilY;
                sq = Jiggle((w - U) * c, 0.2, 0.34);
                lookTY = w > U + 0.06 ? 1 : -1;   // already looking down at where it will fall
            }
            if (w >= F - 0.04 && w < U)
                SetFilm(floor, 3, CX, w < S ? Smooth((w - F + 0.04) / 0.08) : floor.A, w < S ? R * (0.9 + 1.4 * Pop((w - F) / 0.25)) : floor.Spread);
            if (w >= U - 0.04)
                SetFilm(ceiling, 2, CX, Smooth((w - U + 0.04) / 0.08), R * (0.9 + 1.1 * Pop((w - U) / 0.14)));
            px = CX;
            py = y;
            flatY = 1 - sq;
            flatX = 1 + sq * 0.7;
        }

        // From the ceiling, down on a thread that thins and snaps, settling in the middle.
        void DripHome(double v)
        {
            double R = Radius, ceilY = R * 0.8, low = ceilY + R * 1.7;
            axis = 1;
            if (v < 0.4)
            {
                double x = v / 0.4;
                py = ceilY + (low - ceilY) * Smooth(x);
                stick = 1 - Smooth((x - 0.6) / 0.4);
                anchorX = CX;
                anchorY = 0;
                SetFilm(liquid.Films[1], 2, CX, 1 - Smooth(x), R * (0.9 + 1.1 * (1 - Smooth(x))));
                lookTY = 1;
            }
            else
            {
                py = low + (CY - low) * Pop((v - 0.4) / 0.6);
                lookTY = v < 0.75 ? 1 : 0;
            }
        }

        // The circle: a glance up, a slide up to the top, laps round the screen leaving a snail trail, then home.
        void Circle(double u, double len)
        {
            const double glance = 0.45, rise = 0.6;
            Timing tm = Plan(len, glance + rise, 0.8);
            double R = Radius, rx = Width / 2.0 - R * 2.2, ry = Height / 2.0 - R * 2.2;
            if (u < glance) { Glance(u, 0, -1); return; }
            if (u < tm.Pre)
            {
                double x = (u - glance) / rise;
                py = CY - ry * Smoother(x);
                axis = 1;
                lookTY = x < 0.65 ? -1 : 0;
                lookTX = x < 0.65 ? 0 : 1;   // and already looking along the way round
                return;
            }
            double s = u - tm.Pre;
            if (s < tm.Moves)
            {
                double th = -Math.PI / 2 + 2 * Math.PI * tm.Reps * Cruise(s / tm.Moves, Math.Min(0.3, 0.6 / tm.Moves));
                px = CX + rx * Math.Cos(th);
                py = CY + ry * Math.Sin(th);
                double vx = -rx * Math.Sin(th), vy = ry * Math.Cos(th), vl = Math.Max(1e-6, Math.Sqrt(vx * vx + vy * vy));
                lookTX = vx / vl;
                lookTY = vy / vl;
                axis = 2;
                stiff = 1.6;
                trailOn = true;   // only while going round
                return;
            }
            s -= tm.Moves;
            if (s < tm.Back)
            {
                double x = s / tm.Back;
                py = (CY - ry) + ry * Pop(x);
                axis = 1;
                lookTY = x < 0.5 ? 1 : 0;
            }
        }

        // Closing: it becomes a real eye, blinks slowly (shut, a moment, open with a little widening), then melts
        // back into the drop.
        void Blink(double u, double len)
        {
            Timing tm = Plan(len, 0.6, 0.6);
            if (u < tm.Pre) { eyeShape = Smooth(u / tm.Pre); return; }
            double s = u - tm.Pre;
            if (s < tm.Moves)
            {
                eyeShape = 1;
                int i = Math.Min(tm.Reps - 1, (int)(s / tm.Cycle));
                double w = (s - i * tm.Cycle) / tm.Cycle;
                open = w < 0.34 ? 1 - Smooth(w / 0.34) : w < 0.52 ? 0 : Pop((w - 0.52) / 0.48);
                double shut = 1 - Math.Min(1, open);
                lookTY = 0.2 * shut;          // the eye rolls down a little behind the lid
                flatY = 1 - 0.06 * shut;      // and the lid's weight settles the drop a touch
                flatX = 1 + 0.04 * shut;
                py += Radius * 0.05 * shut;
                return;
            }
            s -= tm.Moves;
            eyeShape = s < tm.Back ? 1 - Smooth(s / tm.Back) : 0;
        }

        // ---- Drawing ----

        // The liquid is shown in its own see-through window, moved and sized to just the area it covers each frame,
        // so it floats over the black background or, with that off, over the screen itself.
        void Redraw()
        {
            byte opacity = (byte)(255 * fadeK);
            if (alpha > 0.002f)
            {
                liquid.Render(ClientSize, alpha);
                liquid.Present(drop.Handle, Location, opacity);
                liquidShown = true;
            }
            else if (liquidShown)
            {
                liquid.Present(drop.Handle, Location, 0);
                liquidShown = false;
            }
        }

        // The step's words and the time left, in a see-through window near the bottom. Over the screen (no dark
        // background) they sit on a soft dark band so they stay readable.
        void UpdateCaption()
        {
            int w = Width, h = (int)(captionFont.Height * 1.4 + hintFont.Height * 1.6 + 16);
            if (captionArt == null) captionArt = new Bitmap(w, h, PixelFormat.Format32bppPArgb);
            using (Graphics g = Graphics.FromImage(captionArt))
            {
                g.Clear(Color.Transparent);
                g.SmoothingMode = SmoothingMode.AntiAlias;
                g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;
                if (step >= 0 && outroAt < 0)
                {
                    string text = Lang.T(Steps[step].Text), hint = forced ? Math.Max(0, shownSeconds).ToString() : Lang.F("Esc to skip  ·  {0}", Math.Max(0, shownSeconds));
                    if (!backdrop)
                    {
                        float tw = Math.Max(g.MeasureString(text, captionFont).Width, g.MeasureString(hint, hintFont).Width) + captionFont.Height * 1.6f;
                        using (GraphicsPath p = IconArt.RoundRect(new RectangleF((w - tw) / 2, 0, tw, h), h * 0.3f))
                        using (var b = new SolidBrush(Color.FromArgb(150, 12, 14, 18)))
                            g.FillPath(b, p);
                    }
                    var centred = new StringFormat { Alignment = StringAlignment.Center };
                    using (var b = new SolidBrush(backdrop ? Color.FromArgb(118, 118, 118) : Color.FromArgb(222, 226, 232)))
                        g.DrawString(text, captionFont, b, new RectangleF(0, 8, w, captionFont.Height * 1.4f), centred);
                    using (var b = new SolidBrush(backdrop ? Color.FromArgb(70, 70, 70) : Color.FromArgb(160, 166, 176)))
                        g.DrawString(hint, hintFont, b, new RectangleF(0, 8 + captionFont.Height * 1.5f, w, hintFont.Height * 1.4f), centred);
                }
            }
            ShowCaption();
        }

        void ShowCaption()
        {
            if (captionArt == null || caption == null) return;
            Native.UpdateLayered(caption.Handle, captionArt, new Point(Left, Bottom - captionArt.Height - Height / 14), (byte)(255 * fadeK));
        }

        // A click-through, see-through window above the break, for the liquid or the caption.
        class Layer : Form
        {
            public Layer(Rectangle bounds)
            {
                FormBorderStyle = FormBorderStyle.None;
                ShowInTaskbar = false;
                StartPosition = FormStartPosition.Manual;
                Bounds = bounds;
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

        protected override void OnPaint(PaintEventArgs e)
        {
            e.Graphics.Clear(Color.Black);   // the background only; everything else is in the layers above
        }

        protected override void OnFormClosed(FormClosedEventArgs e)
        {
            frame.Dispose();
            timeEndPeriod(1);
            foreach (Shade sh in others) sh.Close();
            drop.Close();
            caption.Close();
            if (voice != null) voice.Dispose();
            liquid.Dispose();
            if (captionArt != null) captionArt.Dispose();
            captionFont.Dispose();
            hintFont.Dispose();
            base.OnFormClosed(e);
            if (Finished != null) Finished(0);
        }
    }
}
