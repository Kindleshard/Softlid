using System;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Text;
using System.IO;
using System.Windows.Forms;

namespace BlinkBuddy
{
    // A place to judge every engine at every strength on the same picture, and write down what you see. Each rating is
    // a line in %AppData%\BlinkBuddy\tone-feedback.txt. The lab changes the real settings while it is open (the engines
    // work on the whole screen); App puts them back when it closes.
    class ToneLab : Form
    {
        const int W = 1180, H = 720, X0 = 788, PW = 380;   // client size and the control column, in 96-dpi units
        static readonly string[] EngineNames = { "Standard", "Smart (GPU)", "Smart layer", "DesktopLUT" };
        static readonly int[] EngineModes = { 0, 2, 3, 4 };
        static readonly string[][] Answers =
        {
            new[] { "Too bright", "Comfortable", "Too dark" },
            new[] { "Hidden", "Hard to see", "Clear" },
            new[] { "Hard to read", "Fine", "Easy" },
            new[] { "Wrong", "A bit off", "Natural" },
            new[] { "Bad", "OK", "Good", "Great" }
        };
        static readonly string[] Questions = { "White areas", "Dark details (grey steps, dark panel)", "Text", "Colours", "Overall" };
        static readonly string[] AnswerKeys = { "whites", "dark", "text", "colours", "overall" };

        public static string FeedbackFile
        {
            get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "BlinkBuddy", "tone-feedback.txt"); }
        }

        readonly App app;
        readonly Settings s;
        readonly Segmented engine, level;
        readonly Segmented[] answers = new Segmented[Answers.Length];
        readonly TextBox note;
        readonly Label progress, brightText;
        readonly bool[] rated = new bool[16];
        readonly Timer clock = new Timer { Interval = 1000 };
        bool switching;

        public ToneLab(App app)
        {
            this.app = app;
            s = app.S;
            SuspendLayout();
            AutoScaleDimensions = new SizeF(96f, 96f);
            AutoScaleMode = AutoScaleMode.Dpi;
            Text = "BlinkBuddy tone lab";
            Icon = app.WindowIcon;
            FormBorderStyle = FormBorderStyle.FixedSingle;
            MaximizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;
            BackColor = Theme.Bg;
            ForeColor = Theme.Fg;
            Font = Theme.Body;
            ClientSize = new Size(W, H);

            Controls.Add(new TestScene { Bounds = new Rectangle(12, 12, 760, 696) });

            Add(Theme.MakeLabel("Tone test lab", Theme.Heading, Theme.Fg, new Rectangle(X0, 10, PW, 28)));
            Label hint = Theme.MakeLabel("Rate the picture on the left for each engine and strength. Hold the button below to compare " +
                "with no tone. Your own settings come back when you close this window.", Theme.Small, Theme.Dim, new Rectangle(X0, 40, PW, 52));
            hint.TextAlign = ContentAlignment.TopLeft;
            Add(hint);

            Add(Theme.MakeLabel("Engine", Theme.Small, Theme.Dim, new Rectangle(X0, 96, PW, 18)));
            engine = new Segmented("Standard", "Smart GPU", "Layer", "DesktopLUT") { Bounds = new Rectangle(X0, 116, PW, 32) };
            engine.Selected = Array.IndexOf(EngineModes, s.ToneMode) < 0 ? 0 : Array.IndexOf(EngineModes, s.ToneMode);
            Add(engine);
            Add(Theme.MakeLabel("Strength", Theme.Small, Theme.Dim, new Rectangle(X0, 154, PW, 18)));
            level = new Segmented(Comfort.Levels) { Bounds = new Rectangle(X0, 174, PW, 32) };
            level.Selected = s.Level;
            Add(level);

            Add(Theme.MakeLabel("Brightness", Theme.Small, Theme.Dim, new Rectangle(X0, 212, 200, 18)));
            brightText = Theme.MakeLabel(s.Brightness + "%", Theme.Small, Theme.Dim, new Rectangle(X0 + PW - 80, 212, 80, 18));
            brightText.TextAlign = ContentAlignment.MiddleRight;
            Add(brightText);
            var bright = new Slider(20, 100, s.Brightness) { Bounds = new Rectangle(X0 - 8, 230, PW + 16, 30) };
            bright.ValueChanged += delegate
            {
                s.Brightness = bright.Value;
                brightText.Text = bright.Value + "%";
                app.Changed(true);
            };
            Add(bright);

            Button compare = Theme.MakeButton("Hold to see it without tone", new Rectangle(X0, 266, PW, 32));
            compare.MouseDown += delegate { app.LabCompare(true); };
            compare.MouseUp += delegate { app.LabCompare(false); };
            Add(compare);

            progress = Theme.MakeLabel("", Theme.Small, Theme.Dim, new Rectangle(X0, 302, PW, 52));
            progress.TextAlign = ContentAlignment.TopLeft;
            Add(progress);

            for (int i = 0; i < Answers.Length; i++)
            {
                int y = 358 + i * 52;
                Add(Theme.MakeLabel(Questions[i], Theme.Small, Theme.Fg, new Rectangle(X0, y, PW, 18)));
                answers[i] = new Segmented(Answers[i]) { Bounds = new Rectangle(X0, y + 19, PW, 28), Font = Theme.Small };
                answers[i].Selected = -1;   // nothing chosen yet
                Add(answers[i]);
            }
            note = new TextBox
            {
                Bounds = new Rectangle(X0, 622, PW, 26), Font = Theme.Small,
                BackColor = Color.FromArgb(45, 45, 45), ForeColor = Theme.Fg, BorderStyle = BorderStyle.FixedSingle
            };
            note.HandleCreated += delegate { Native.SendMessage(note.Handle, 0x1501, new IntPtr(1), "Anything else? (optional)"); };   // EM_SETCUEBANNER
            Add(note);
            Button save = Theme.MakeButton("Save and next", new Rectangle(X0, 658, 170, 34));
            Button skip = Theme.MakeButton("Skip", new Rectangle(X0 + 180, 658, 80, 34));
            Button open = Theme.MakeButton("Open answers", new Rectangle(X0 + 270, 658, 110, 34));
            open.Font = Theme.Small;
            save.Click += delegate { Save(); };
            skip.Click += delegate { Next(); };
            open.Click += delegate
            {
                if (File.Exists(FeedbackFile)) Process.Start(new ProcessStartInfo(FeedbackFile) { UseShellExecute = true });
            };
            Add(save);
            Add(skip);
            Add(open);
            AcceptButton = save;
            ResumeLayout(false);

            engine.SelectedChanged += delegate { ShowCombo(engine.Selected, level.Selected); };
            level.SelectedChanged += delegate { ShowCombo(engine.Selected, level.Selected); };
            clock.Tick += delegate { UpdateProgress(""); };
            clock.Start();
            ShowCombo(engine.Selected, level.Selected);
        }

        void Add(Control c) { Controls.Add(c); }

        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            Native.DarkTitleBar(Handle);
        }

        protected override void OnFormClosed(FormClosedEventArgs e)
        {
            clock.Dispose();
            base.OnFormClosed(e);
        }

        // Puts this engine and strength on screen and starts a fresh set of answers for it.
        void ShowCombo(int e, int l)
        {
            if (switching) return;
            switching = true;
            engine.Selected = e;
            level.Selected = l;
            switching = false;
            s.ComfortLevel = l;
            if (s.ToneMode != EngineModes[e]) app.SetToneMode(EngineModes[e]);
            else app.Changed(true);
            foreach (Segmented a in answers) a.Selected = -1;
            note.Text = "";
            UpdateProgress("");
        }

        void UpdateProgress(string message)
        {
            int done = 0;
            foreach (bool r in rated) if (r) done++;
            string state = EngineNames[engine.Selected] + ", " + Comfort.Levels[level.Selected] + (rated[Index] ? " (rated)" : "") +
                ". Rated " + done + " of 16.";
            if (EngineModes[engine.Selected] == 4 && app.LutStatus.Length > 0) state += "\n" + app.LutStatus;
            if (app.GuardNote.Length > 0) state += "\n" + app.GuardNote;
            progress.Text = message.Length > 0 ? message + "\n" + state : state;
        }

        int Index { get { return engine.Selected * 4 + level.Selected; } }

        void Save()
        {
            foreach (Segmented a in answers)
                if (a.Selected < 0)
                {
                    UpdateProgress("Answer all five first.");
                    return;
                }
            var line = new System.Text.StringBuilder();
            line.Append(DateTime.Now.ToString("yyyy-MM-dd HH:mm"))
                .Append(" | ").Append(EngineNames[engine.Selected])
                .Append(" | ").Append(Comfort.Levels[level.Selected])
                .Append(" | brightness ").Append(s.Brightness).Append(s.HardwareBrightness ? "%, backlight " + s.Backlight + "%" : "%")
                .Append(" | blue filter ").Append(s.BlueFilter).Append('%');
            for (int i = 0; i < answers.Length; i++)
                line.Append(" | ").Append(AnswerKeys[i]).Append('=').Append(Answers[i][answers[i].Selected]);
            if (app.GuardNote.Length > 0) line.Append(" | ").Append(app.GuardNote);
            if (note.Text.Trim().Length > 0) line.Append(" | note: ").Append(note.Text.Trim().Replace('|', '/'));
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(FeedbackFile));
                File.AppendAllText(FeedbackFile, line + Environment.NewLine);
            }
            catch (Exception ex)
            {
                UpdateProgress("Couldn't save: " + ex.Message);
                return;
            }
            rated[Index] = true;
            Next();
        }

        // The next combination not rated yet, engine by engine.
        void Next()
        {
            for (int step = 1; step <= 16; step++)
            {
                int i = (Index + step) % 16;
                if (rated[i]) continue;
                ShowCombo(i / 4, i % 4);
                return;
            }
            UpdateProgress("All 16 rated, thank you. Tell Claude you're done.");
        }
    }

    // The picture the lab is judged on: a bright document, a dark editor, grey steps, colour chips and a photo-like
    // scene, each with the details a tone curve tends to lose (near-white and near-black steps, dim text).
    class TestScene : PaintedControl
    {
        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.TextRenderingHint = TextRenderingHint.ClearTypeGridFit;
            float k = Width / 760f;
            g.ScaleTransform(k, k);
            g.Clear(Color.FromArgb(43, 43, 43));
            using (var title = new Font("Segoe UI Semibold", 20f, GraphicsUnit.Pixel))
            using (var body = new Font("Segoe UI", 13f, GraphicsUnit.Pixel))
            using (var small = new Font("Segoe UI", 11f, GraphicsUnit.Pixel))
            using (var link = new Font("Segoe UI", 13f, FontStyle.Underline, GraphicsUnit.Pixel))
            using (var code = new Font("Consolas", 12f, GraphicsUnit.Pixel))
            {
                Document(g, title, body, small, link);
                Editor(g, code, small);
                Ramp(g, small);
                Chips(g, small);
                Photo(g, small);
            }
        }

        static void Fill(Graphics g, int rgb, float x, float y, float w, float h)
        {
            using (var b = new SolidBrush(Color.FromArgb(rgb))) g.FillRectangle(b, x, y, w, h);
        }

        static void Write(Graphics g, string text, Font f, int rgb, float x, float y, float w = 0, float h = 0)
        {
            using (var b = new SolidBrush(Color.FromArgb(rgb)))
                if (w > 0) g.DrawString(text, f, b, new RectangleF(x, y, w, h));
                else g.DrawString(text, f, b, x, y);
        }

        static void Document(Graphics g, Font title, Font body, Font small, Font link)
        {
            Fill(g, unchecked((int)0xFFFFFFFF), 0, 0, 470, 330);
            Write(g, "Quarterly notes", title, unchecked((int)0xFF111111), 20, 14);
            Write(g, "Bright pages like this one are where a tone curve matters most. The page should feel calm rather than " +
                "glaring, while the text stays crisp and easy to follow from line to line.", body, unchecked((int)0xFF222222), 20, 48, 430, 70);
            Write(g, "Secondary text in grey  ·  updated 3 minutes ago", small, unchecked((int)0xFF6B6B6B), 20, 124);
            Write(g, "A blue link, to check colour and contrast", link, unchecked((int)0xFF0B66D0), 20, 144);
            string[,] rows = { { "Screen time", "6 h 40 min" }, { "Breaks taken", "14 of 18" }, { "Blink reminders", "212" }, { "Night mode", "21:00 to 07:00" } };
            for (int i = 0; i < 4; i++)
            {
                float y = 172 + i * 24;
                Fill(g, i % 2 == 0 ? unchecked((int)0xFFFFFFFF) : unchecked((int)0xFFF3F3F3), 20, y, 430, 24);
                using (var pen = new Pen(Color.FromArgb(227, 227, 227))) g.DrawLine(pen, 20, y + 24, 450, y + 24);
                Write(g, rows[i, 0], body, unchecked((int)0xFF333333), 26, y + 3);
                Write(g, rows[i, 1], body, unchecked((int)0xFF333333), 300, y + 3);
            }
            Write(g, "Near-white steps", small, unchecked((int)0xFF6B6B6B), 20, 276);
            int[] near = { 255, 247, 238, 228, 216, 200 };
            for (int i = 0; i < near.Length; i++)
            {
                Fill(g, Color.FromArgb(near[i], near[i], near[i]).ToArgb(), 20 + i * 72, 294, 68, 28);
                using (var pen = new Pen(Color.FromArgb(200, 200, 200))) g.DrawRectangle(pen, 20 + i * 72, 294, 68, 28);
                Write(g, near[i].ToString(), small, unchecked((int)0xFF555555), 26 + i * 72, 300);
            }
        }

        static void Editor(Graphics g, Font code, Font small)
        {
            Fill(g, unchecked((int)0xFF1E1E1E), 480, 0, 280, 330);
            Fill(g, unchecked((int)0xFF2D2D2D), 480, 0, 280, 24);
            Write(g, "reminders.cs", small, unchecked((int)0xFFCCCCCC), 490, 5);
            var lines = new[]
            {
                new[] { "// dim green comment", "FF6A9955" },
                new[] { "void Blink(int seconds)", "FF569CD6" },
                new[] { "{", "FFD4D4D4" },
                new[] { "    var text = \"look away\";", "FFCE9178" },
                new[] { "    Remind(text, 20);", "FFDCDCAA" },
                new[] { "    count += 1;", "FF9CDCFE" },
                new[] { "}", "FFD4D4D4" },
                new[] { "// faint text, hardest to see", "FF4A4A4A" }
            };
            for (int i = 0; i < lines.Length; i++)
            {
                float y = 34 + i * 22;
                Write(g, (i + 1).ToString(), code, unchecked((int)0xFF5A5A5A), 488, y);
                Write(g, lines[i][0], code, Convert.ToInt32(lines[i][1], 16), 512, y);
            }
            Write(g, "Near-black steps", small, unchecked((int)0xFF8A8A8A), 490, 250);
            int[] dark = { 0, 10, 20, 31, 42, 54 };
            for (int i = 0; i < dark.Length; i++)
            {
                Fill(g, Color.FromArgb(dark[i], dark[i], dark[i]).ToArgb(), 490 + i * 44, 270, 40, 32);
                Write(g, dark[i].ToString(), small, unchecked((int)0xFF8A8A8A), 494 + i * 44, 306);
            }
        }

        static void Ramp(Graphics g, Font small)
        {
            Write(g, "Grey steps, black to white", small, unchecked((int)0xFF9A9A9A), 0, 338);
            float w = 760f / 17;
            for (int i = 0; i <= 16; i++)
            {
                int v = Math.Min(255, i * 16);
                Fill(g, Color.FromArgb(v, v, v).ToArgb(), i * w, 356, w + 1, 36);
                Write(g, v.ToString(), small, unchecked((int)0xFF9A9A9A), i * w + 4, 394);
            }
        }

        static void Chips(Graphics g, Font small)
        {
            Write(g, "Strong colours, then soft colours, skin tones and dark colours", small, unchecked((int)0xFF9A9A9A), 0, 414);
            int[] strong = { 0xFF0000, 0x00C800, 0x0050FF, 0xFFE600, 0x00E5FF, 0xFF00D4, 0xFF8A00, 0x8A2BE2 };
            int[] soft = { 0xF8BBD0, 0xBBDEFB, 0xC8E6C9, 0xE1BEE7, 0xF1C27D, 0xE0AC69, 0x8D5524, 0x1A237E, 0x5D1010, 0x1B4D20 };
            for (int i = 0; i < strong.Length; i++) Fill(g, unchecked((int)0xFF000000) | strong[i], i * 95, 432, 91, 34);
            for (int i = 0; i < soft.Length; i++) Fill(g, unchecked((int)0xFF000000) | soft[i], i * 76, 470, 72, 34);
        }

        static void Photo(Graphics g, Font small)
        {
            var area = new RectangleF(0, 514, 760, 182);
            using (var sky = new LinearGradientBrush(area, Color.FromArgb(46, 111, 216), Color.FromArgb(246, 211, 101), 90f))
                g.FillRectangle(sky, area);
            using (var path = new GraphicsPath())
            {
                path.AddEllipse(560, 534, 90, 90);
                using (var sun = new PathGradientBrush(path) { CenterColor = Color.White, SurroundColors = new[] { Color.FromArgb(0, 255, 240, 180) } })
                    g.FillPath(sun, path);
            }
            using (var hills = new GraphicsPath())
            {
                hills.AddPolygon(new[] { new PointF(0, 640), new PointF(140, 600), new PointF(300, 630), new PointF(470, 590),
                    new PointF(640, 625), new PointF(760, 605), new PointF(760, 696), new PointF(0, 696) });
                using (var b = new LinearGradientBrush(new RectangleF(0, 590, 760, 106), Color.FromArgb(46, 94, 52), Color.FromArgb(10, 26, 12), 90f))
                    g.FillPath(b, hills);
            }
            Fill(g, unchecked((int)0xFF050505), 110, 610, 8, 70);
            using (var b = new SolidBrush(Color.FromArgb(12, 12, 12))) g.FillEllipse(b, 88, 572, 52, 52);
            Write(g, "Photo-like scene: sun highlight, sky gradient, dark ground", small, unchecked((int)0xFFFFFFFF), 8, 518);
        }
    }
}
