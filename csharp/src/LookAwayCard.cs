using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Media;
using System.Text;
using System.Windows.Forms;

namespace BlinkBuddy
{
    // Centred countdown card for a stand-up break, or a look-away break without exercises (those take the whole
    // screen: see GuidedBreak). It never takes focus, so typing isn't interrupted.
    class LookAwayCard : Form
    {
        public Action<int> Finished;   // receives the snooze length in seconds, or 0
        readonly Timer timer = new Timer();
        readonly List<Shade> shades = new List<Shade>();
        readonly Label title, hint, count;
        readonly ProgressLine bar;
        readonly EyeArt art;
        readonly int total;
        readonly bool sounds, strict;
        int left, snooze;
        bool over;

        // seconds and caption: a break of another length with its own words (the focus timer's rest).
        public LookAwayCard(bool standUp, Settings s, int seconds = 0, string caption = null, string note = null)
        {
            total = left = seconds > 0 ? seconds : standUp ? s.LongBreakDuration * 60 : s.BreakDuration;
            sounds = s.Sounds;
            strict = s.BreakBackdrop;
            SuspendLayout();
            AutoScaleDimensions = new SizeF(96f, 96f);
            AutoScaleMode = AutoScaleMode.Dpi;
            FormBorderStyle = FormBorderStyle.None;
            ShowInTaskbar = false;
            StartPosition = FormStartPosition.Manual;
            BackColor = Theme.Card;
            ForeColor = Theme.Fg;
            Font = Theme.Body;
            ClientSize = new Size(400, 216);

            art = new EyeArt { Bounds = new Rectangle(18, 16, 64, 44), BackColor = Theme.Card };
            art.Set(0.9f, 0f, 1f);
            title = Theme.MakeLabel(caption ?? (standUp ? "Time for a stand-up break" : "Look away from the screen"),
                Theme.Heading, Theme.Fg, new Rectangle(90, 16, 294, 28));
            hint = Theme.MakeLabel(note ?? (standUp ? "Get up, stretch, walk around, maybe refill your water."
                : "Find something at least 6 m (20 ft) away and let your eyes relax."),
                Theme.Small, Theme.Dim, new Rectangle(90, 44, 294, 36));
            hint.TextAlign = ContentAlignment.TopLeft;
            count = Theme.MakeLabel(Format(left), new Font("Segoe UI Light", 28f), Theme.Fg, new Rectangle(24, 84, 352, 56));
            count.TextAlign = ContentAlignment.MiddleCenter;
            bar = new ProgressLine { Bounds = new Rectangle(24, 148, 352, 4), BackColor = Theme.Card };
            Button snoozeButton = Theme.MakeButton("Snooze 5 min", new Rectangle(160, 166, 120, 34));
            Button skip = Theme.MakeButton("Skip", new Rectangle(288, 166, 88, 34));
            snoozeButton.Click += delegate { snooze = 300; Close(); };
            skip.Click += delegate { Close(); };
            Controls.AddRange(new Control[] { art, title, hint, count, bar, snoozeButton, skip });
            snoozeButton.Visible = skip.Visible = !s.ForceRest;   // forced rest: it runs to the end
            ResumeLayout(false);

            timer.Interval = 1000;
            timer.Tick += delegate { Step(); };
        }

        string Format(int seconds) { return total >= 60 ? string.Format("{0}:{1:00}", seconds / 60, seconds % 60) : seconds.ToString(); }

        protected override bool ShowWithoutActivation { get { return true; } }

        protected override CreateParams CreateParams
        {
            get
            {
                CreateParams cp = base.CreateParams;
                cp.ExStyle |= Native.WS_EX_TOPMOST | Native.WS_EX_TOOLWINDOW | Native.WS_EX_NOACTIVATE;
                return cp;
            }
        }

        protected override void OnLoad(EventArgs e)
        {
            base.OnLoad(e);
            Rectangle wa = Screen.FromPoint(Cursor.Position).WorkingArea;
            Location = new Point(wa.Left + (wa.Width - Width) / 2, wa.Top + (wa.Height - Height) / 2);
            Native.RoundCorners(Handle);
            // Shown before the card itself so the card lands above them.
            if (strict)
                foreach (Screen sc in Screen.AllScreens)
                {
                    var shade = new Shade(sc.Bounds, Color.Black, 0.85);
                    shades.Add(shade);
                    shade.Show();
                }
        }

        protected override void OnShown(EventArgs e)
        {
            base.OnShown(e);
            Native.BringToTopNoActivate(Handle);
            if (sounds) Chime.Play(false);
            timer.Start();
        }

        void Step()
        {
            if (over) { Close(); return; }
            left--;
            bar.Progress = 1f - left / (float)total;
            if (left > 0) { count.Text = Format(left); return; }
            // You're looking away, so the end is announced by sound and the card lingers briefly.
            over = true;
            art.Set(0f, 0f, 1f);
            title.Text = Lang.T("Welcome back!");
            hint.Text = Lang.T("Break done. Blink a few times and carry on.");
            count.Text = Lang.T("Done");
            if (sounds) Chime.Play(true);
            timer.Interval = 2500;
        }

        protected override void OnFormClosed(FormClosedEventArgs e)
        {
            timer.Dispose();
            foreach (Shade shade in shades) shade.Close();
            base.OnFormClosed(e);
            if (Finished != null) Finished(snooze);
        }
    }

    // A soft two-note chime synthesised in memory: falling = look away, rising = come back.
    static class Chime
    {
        static SoundPlayer player;

        public static void Play(bool rising)
        {
            const int rate = 22050;
            double[] notes = rising ? new[] { 659.25, 987.77 } : new[] { 987.77, 659.25 }; // E5, B5
            int step = rate / 6, len = step * notes.Length + rate / 2;
            var pcm = new double[len];
            for (int n = 0; n < notes.Length; n++)
                for (int i = n * step; i < len; i++)
                {
                    double t = (i - n * step) / (double)rate;
                    pcm[i] += 6000 * Math.Min(1, t * 200) * Math.Exp(-t * 7) * Math.Sin(2 * Math.PI * notes[n] * t);
                }

            var ms = new MemoryStream();
            var w = new BinaryWriter(ms);
            w.Write(Encoding.ASCII.GetBytes("RIFF")); w.Write(36 + len * 2);
            w.Write(Encoding.ASCII.GetBytes("WAVEfmt ")); w.Write(16);
            w.Write((short)1); w.Write((short)1); w.Write(rate); w.Write(rate * 2); w.Write((short)2); w.Write((short)16);
            w.Write(Encoding.ASCII.GetBytes("data")); w.Write(len * 2);
            foreach (double v in pcm) w.Write((short)Math.Max(short.MinValue, Math.Min(short.MaxValue, v)));
            ms.Position = 0;

            if (player != null) player.Dispose();
            player = new SoundPlayer(ms);
            player.Play();
        }
    }
}
