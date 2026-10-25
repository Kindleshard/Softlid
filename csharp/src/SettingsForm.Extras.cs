using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Linq;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace BlinkBuddy
{
    // The Extras page: the reading strip, the focus timer, modes for apps and film grain.
    partial class SettingsForm
    {
        // ---- Extras: reading strip, focus timer, modes for apps and film grain ----

        static readonly string[] ShapeNames = { "Band", "Column", "Rectangle", "Circle" };

        void BuildExtrasPage(Stack st)
        {
            stripToggle = Header(st, "E736", "Reading strip", "Dims everything but a clear area that follows your mouse.", s.StripEnabled,
                v => { s.StripEnabled = v; app.Changed(true); });
            // its shape, each shown as a little screen with its clear area
            Row shapeRow = st.Add(64);
            AddLabel(shapeRow, "Shape", Theme.Body, Theme.Fg, 24, 18, 124, 26);
            var shape = new IconPicker(4, s.StripShape, () => 1f, () => 0f) { Bounds = new Rectangle(150, 6, 4 * 48 + 3 * 13 + 2, 48) };
            shape.Painter = PaintShape;
            shape.HoverChanged += delegate { tips.SetToolTip(shape, shape.Hovered >= 0 ? ShapeNames[shape.Hovered] : ""); };
            shapeRow.Controls.Add(shape);
            SliderRow(st, "Strip height", 40, 400, s.StripHeight, defaults.StripHeight, v => v + " px",
                v => { s.StripHeight = v; StripTouched(); }, () => s.StripShape == 0 || s.StripShape == 2);
            SliderRow(st, "Size", 40, 400, s.StripHeight, defaults.StripHeight, v => v + " px",
                v => { s.StripHeight = v; StripTouched(); }, () => s.StripShape == 3);
            SliderRow(st, "Width", 80, 1600, s.StripWidth, defaults.StripWidth, v => v + " px",
                v => { s.StripWidth = v; StripTouched(); }, () => s.StripShape == 1 || s.StripShape == 2);
            SliderRow(st, "Dim outside the strip", 30, 95, s.StripDim, defaults.StripDim, v => v + "%",
                v => { s.StripDim = v; StripTouched(); });
            Row tintRow = st.Add(46);
            AddLabel(tintRow, "Tint", Theme.Body, Theme.Fg, 24, 10, 200, 26);
            var tint = new ColorButton { Bounds = new Rectangle(Edge - 36, 6, 34, 34), Value = Theme.Rgb(s.StripColor), Presets = Theme.StripColors };
            tips.SetToolTip(tint, "Choose a color");
            tint.ColorChanged += delegate { s.StripColor = tint.Value.ToArgb(); StripTouched(); };
            tintRow.Controls.Add(tint);

            Func<bool> more = More(st, "strip");
            // the clear area's edge: none, soft, or an outline (one or the other)
            Row edgeRow = st.Add(46, more);
            AddLabel(edgeRow, "Edge", Theme.Body, Theme.Fg, 24, 10, 120, 26);
            var edge = new Segmented("None", "Soft", "Outline") { Bounds = new Rectangle(150, 6, Edge - 150, 34) };
            edge.Selected = s.StripEdge;
            edge.SelectedChanged += delegate { s.StripEdge = edge.Selected; StripTouched(); UpdateRows(); };
            edgeRow.Controls.Add(edge);
            SliderRow(st, "Softness", 4, 200, s.StripFeather, defaults.StripFeather, v => v + " px",
                v => { s.StripFeather = v; StripTouched(); }, And(more, () => s.StripEdge == 1));
            Slider outline = SliderRow(st, "Outline", 1, 12, s.StripBorder, defaults.StripBorder, v => v + " px",
                v => { s.StripBorder = v; StripTouched(); }, And(more, () => s.StripEdge == 2));
            outline.Width -= 44;   // room for its colour, between it and the reset button
            var line = new ColorButton
            {
                Bounds = new Rectangle(Edge - 70, 27, 32, 32), AllowAccent = true, UseAccent = s.StripLine == 0,
                Value = s.StripLine == 0 ? Theme.Accent : Theme.Rgb(s.StripLine)
            };
            tips.SetToolTip(line, "Outline color");
            line.ColorChanged += delegate { s.StripLine = line.UseAccent ? 0 : line.Value.ToArgb(); StripTouched(); };
            outline.Parent.Controls.Add(line);
            // a band or column can dim just one side
            Row sidesRow = st.Add(46, And(more, () => s.StripShape <= 1));
            AddLabel(sidesRow, "Dimmed sides", Theme.Body, Theme.Fg, 24, 10, 120, 26);
            var sides = new DropDown(SideNames()) { Bounds = new Rectangle(150, 6, Edge - 150, 34) };
            sides.Selected = s.StripCover;
            sides.SelectedChanged += delegate { s.StripCover = sides.Selected; StripTouched(); };
            sidesRow.Controls.Add(sides);
            shape.SelectedChanged += delegate
            {
                s.StripShape = shape.Selected;
                sides.SetItems(SideNames());
                StripTouched();
                UpdateRows();
            };
            // following the mouse, or staying put and moving with the keys
            Row moveRow = st.Add(46, more);
            AddLabel(moveRow, "Movement", Theme.Body, Theme.Fg, 24, 10, 120, 26);
            var move = new DropDown("Follow the mouse", "Move with keys") { Bounds = new Rectangle(150, 6, Edge - 150, 34) };
            move.Selected = s.StripFollow ? 0 : 1;
            move.SelectedChanged += delegate { s.StripFollow = move.Selected == 0; StripTouched(); UpdateRows(); };
            moveRow.Controls.Add(move);
            Func<bool> keys = And(more, () => !s.StripFollow);
            Note(st, "Ctrl+Alt with the arrow keys moves it; Ctrl+Alt with + and - changes its size (see Keyboard shortcuts in Options).", 2, keys);
            SliderRow(st, "Speed up held keys", 0, 100, s.StripAccel, 50, v => v == 0 ? "Off" : v + "%",
                v => { s.StripAccel = v; app.Changed(false); }, keys);
            ToggleRow(st, "Stay on one monitor", s.StripOneMonitor, v => { s.StripOneMonitor = v; StripTouched(); }, more);

            BuildFocusTimer(st);
            BuildRules(st);

            Func<bool> grainShown = () => s.AdvancedView;
            Divider(st, grainShown);
            grainToggle = Header(st, "E81E", "Film grain", "Fine noise over the whole screen, like film.", s.GrainEnabled,
                v => { s.GrainEnabled = v; app.Changed(true); }, grainShown);
            SliderRow(st, "Grain strength", 5, 100, s.GrainStrength, defaults.GrainStrength, v => v + "%",
                v => { s.GrainStrength = v; GrainTouched(); }, grainShown);
            Row toneRow = st.Add(44, grainShown);
            AddLabel(toneRow, "Grain tone", Theme.Body, Theme.Fg, 24, 8, 120, 26);
            var tone = new Segmented("Darker", "Mixed", "Lighter") { Bounds = new Rectangle(150, 4, Edge - 150, 32) };
            tone.Selected = s.GrainTone;
            tone.SelectedChanged += delegate { s.GrainTone = tone.Selected; GrainTouched(); };
            toneRow.Controls.Add(tone);
            ToggleRow(st, "Coarser", s.GrainCoarse, v => { s.GrainCoarse = v; GrainTouched(); }, grainShown);
            ToggleRow(st, "Animated (more power)", s.GrainAnimated, v => { s.GrainAnimated = v; GrainTouched(); }, grainShown);
            st.Add(16);
        }

        string[] SideNames()
        {
            return s.StripShape == 1 ? new[] { "Left and right", "Left only", "Right only" } : new[] { "Above and below", "Above only", "Below only" };
        }

        // A little dimmed screen with shape i's clear area in it, and a few lines of text showing through.
        void PaintShape(Graphics g, RectangleF r, int i, float hover)
        {
            using (GraphicsPath screen = IconArt.RoundRect(r, r.Height * 0.14f))
            {
                Region old = g.Clip;
                g.SetClip(screen, CombineMode.Intersect);
                using (var dimmed = new SolidBrush(Color.FromArgb(30, 30, 34))) g.FillRectangle(dimmed, r);
                float w = r.Width, h = r.Height;
                RectangleF c = i == 0 ? new RectangleF(r.X - 1, r.Y + h * 0.36f, w + 2, h * 0.28f)
                    : i == 1 ? new RectangleF(r.X + w * 0.34f, r.Y - 1, w * 0.32f, h + 2)
                    : i == 2 ? new RectangleF(r.X + w * 0.12f, r.Y + h * 0.27f, w * 0.76f, h * 0.46f)
                    : new RectangleF(r.X + w * 0.2f, r.Y + h * 0.2f, w * 0.6f, h * 0.6f);
                int light = (int)(185 + 50 * hover);
                using (var clear = new SolidBrush(Color.FromArgb(light, light, Math.Min(255, light + 4))))
                {
                    if (i == 3) g.FillEllipse(clear, c);
                    else if (i == 2) using (GraphicsPath p = IconArt.RoundRect(c, c.Height * 0.22f)) g.FillPath(clear, p);
                    else g.FillRectangle(clear, c);
                }
                using (var ink = new SolidBrush(Color.FromArgb(150, 50, 50, 60)))
                {
                    float x0 = Math.Max(c.Left, r.Left) + w * 0.1f, x1 = Math.Min(c.Right, r.Right) - w * 0.1f, cy = c.Top + c.Height / 2;
                    for (int k = -1; k <= 1; k++)
                    {
                        float y = cy + k * h * 0.08f - h * 0.012f;
                        if (y < c.Top + h * 0.03f || y > c.Bottom - h * 0.05f) continue;
                        float a = x0, b = x1;
                        if (i == 3)
                        {
                            double half = Math.Sqrt(Math.Max(0, c.Width * c.Width / 4 - (y - cy) * (y - cy)));
                            a = (float)(c.Left + c.Width / 2 - half) + w * 0.06f;
                            b = (float)(c.Left + c.Width / 2 + half) - w * 0.06f;
                        }
                        if (b > a) g.FillRectangle(ink, a, y, (b - a) * (k == 1 ? 0.6f : 1f), h * 0.025f);
                    }
                }
                g.Clip = old;
            }
        }

        // ---- The focus timer: work, then rest, again and again (the Pomodoro way) ----

        FocusRing focusRing;

        void BuildFocusTimer(Stack st)
        {
            Divider(st);
            SectionHead(st, "E916", "Focus timer", "Work, then rest, again and again.", null);
            Row card = st.Add(162);
            focusRing = new FocusRing
            {
                Bounds = new Rectangle(24, 4, 154, 154),
                Progress = () => app.FocusState == App.FocusResting ? 1f
                    : app.FocusState == App.FocusOff ? 0f : 1f - app.FocusLeft / (float)Math.Max(1, s.FocusWork * 60),
                TimeText = () => app.FocusState == App.FocusResting ? Lang.T("Rest") : app.FocusText(),
                StateText = () => Lang.T(app.FocusState == App.FocusWorking ? "Focus" : app.FocusState == App.FocusPaused ? "Paused"
                    : app.FocusState == App.FocusResting ? "Resting" : "Ready"),
                Ink = () => app.FocusState == App.FocusResting ? Theme.Good : app.FocusState == App.FocusPaused ? Theme.Dim : Theme.Accent,
                Done = () => app.FocusDone == 0 ? 0 : (app.FocusDone - 1) % 4 + 1
            };
            tips.SetToolTip(focusRing, "The dots: work periods done, in rounds of four");
            card.Controls.Add(focusRing);
            focusButton = new RoundButton { Text = "Start", Bounds = new Rectangle(200, 24, Edge - 42 - 12 - 200, 42), Tint = Theme.Accent };
            focusButton.Click += delegate
            {
                if (app.FocusState == App.FocusWorking) app.FocusPause(); else app.FocusStart();
                RefreshStatus();
            };
            var stop = new RoundButton { Text = "", Font = Theme.Glyphs, Bounds = new Rectangle(Edge - 42, 24, 42, 42) };
            tips.SetToolTip(stop, "Stop");
            stop.Click += delegate { app.FocusStop(); RefreshStatus(); };
            var work = new Stepper("Work", 5, 90, 5, s.FocusWork, v => Lang.F("{0} min", v)) { Bounds = new Rectangle(200, 86, 122, 62) };
            work.ValueChanged += delegate { s.FocusWork = work.Value; app.Changed(false); RefreshStatus(); };
            var rest = new Stepper("Rest", 1, 30, 1, s.FocusRest, v => Lang.F("{0} min", v)) { Bounds = new Rectangle(Edge - 122, 86, 122, 62) };
            rest.ValueChanged += delegate { s.FocusRest = rest.Value; app.Changed(false); };
            card.Controls.Add(focusButton);
            card.Controls.Add(stop);
            card.Controls.Add(work);
            card.Controls.Add(rest);
            Note(st, "While it runs, it takes over from the look-away and stand-up reminders; blink reminders go on.", 2);
        }

        void GrainTouched()
        {
            if (!s.GrainEnabled) grainToggle.Checked = true;
            else app.Changed(true);
        }

        void StripTouched()
        {
            if (!s.StripEnabled) stripToggle.Checked = true;
            else app.Changed(true);
        }

        // ---- Modes for apps: a mode that applies while an app is in front ----

        Row rulesRow;

        void BuildRules(Stack st)
        {
            Func<bool> advanced = () => s.AdvancedView;   // (in Simple they stay listed but idle)
            Divider(st, advanced);
            SectionHead(st, "E7F4", "Modes for apps", "While one of these apps is in front, its mode is used.", advanced);
            rulesRow = st.Add(30, advanced);
            Row addRow = st.Add(52, advanced);
            var add = new RoundButton { Text = "Add an app...", Bounds = new Rectangle(24, 8, 170, 34) };
            add.Click += delegate
            {
                List<Modes.Rule> rules = Modes.Rules(s);
                using (var d = new AppRuleDialog(rules.Select(r => r.App).ToList()))
                {
                    if (d.ShowDialog(this) != DialogResult.OK) return;
                    Modes.Rule added = d.Result;
                    rules.RemoveAll(r => r.App == added.App);
                    rules.Add(added);
                }
                Modes.SaveRules(s, rules);
                app.Changed(false);
                ShowRules();
            };
            addRow.Controls.Add(add);
            Note(st, "The night schedule doesn't change an app's mode, so Editing keeps true color at night too.", 2, advanced);
            ShowRules();
        }

        // One line per rule: the app's icon, its name with its file under it, its mode (a drop-down) and a remove button.
        void ShowRules()
        {
            foreach (Control c in rulesRow.Controls.Cast<Control>().ToArray())
            {
                var pic = c as PictureBox;
                if (pic != null && pic.Image != null) pic.Image.Dispose();
                c.Dispose();
            }
            float k = rulesRow.Width / (float)W;   // built before or after the window is scaled for the display
            List<Modes.Rule> rules = Modes.Rules(s);
            Func<int, int> px = v => (int)Math.Round(v * k);
            if (rules.Count == 0)
            {
                rulesRow.Controls.Add(Theme.MakeLabel("No apps yet.", Theme.Small, Theme.Dim, new Rectangle(px(24), px(4), px(Edge - 24), px(22))));
                rulesRow.Height = px(30);
            }
            var modeNames = new string[Modes.Custom];   // every mode but Custom (your own settings: the same as no rule)
            Array.Copy(Modes.Names, modeNames, Modes.Custom);
            for (int i = 0; i < rules.Count; i++)
            {
                Modes.Rule rule = rules[i];
                int y = i * 52;
                Bitmap icon = Modes.IconOf(rule.Path, px(24));
                if (icon != null)
                    rulesRow.Controls.Add(new PictureBox { Image = icon, SizeMode = PictureBoxSizeMode.CenterImage, BackColor = Theme.Bg,
                        Bounds = new Rectangle(px(24), px(y + 12), px(26), px(26)) });
                else
                {
                    Label glyph = Theme.MakeLabel("", Theme.SectionIcons, Theme.Dim, new Rectangle(px(22), px(y + 10), px(30), px(30)));
                    glyph.TextAlign = ContentAlignment.MiddleCenter;
                    rulesRow.Controls.Add(glyph);
                }
                // (names set after: an app called "Game" mustn't be translated)
                Label name = Theme.MakeLabel("", Theme.Body, Theme.Fg, new Rectangle(px(62), px(y + 5), px(Edge - 62 - 220), px(24)));
                name.Text = Modes.NameOf(rule.App, rule.Path);
                name.AutoEllipsis = true;
                Label file = Theme.MakeLabel("", Theme.Small, Theme.Dim, new Rectangle(px(62), px(y + 28), px(Edge - 62 - 220), px(18)));
                file.Text = rule.App;
                file.AutoEllipsis = true;
                rulesRow.Controls.Add(name);
                rulesRow.Controls.Add(file);
                var mode = new DropDown(modeNames) { Bounds = new Rectangle(px(Edge - 208), px(y + 9), px(162), px(34)) };
                mode.Selected = Math.Max(0, Math.Min(Modes.Custom - 1, rule.Mode));
                mode.SelectedChanged += delegate
                {
                    List<Modes.Rule> all = Modes.Rules(s);
                    int at = all.FindIndex(r => r.App == rule.App);
                    if (at >= 0) all[at] = new Modes.Rule { App = rule.App, Mode = mode.Selected, Path = rule.Path };
                    Modes.SaveRules(s, all);
                    app.Changed(false);
                };
                rulesRow.Controls.Add(mode);
                var remove = new RoundButton { Text = "", Font = Theme.Glyphs, Bounds = new Rectangle(px(Edge - 36), px(y + 10), px(32), px(32)) };
                tips.SetToolTip(remove, "Remove");
                remove.Click += delegate
                {
                    List<Modes.Rule> all = Modes.Rules(s);
                    all.RemoveAll(r => r.App == rule.App);
                    Modes.SaveRules(s, all);
                    app.Changed(false);
                    BeginInvoke((Action)ShowRules);   // not while this button's own click is running
                };
                rulesRow.Controls.Add(remove);
                rulesRow.Height = px(rules.Count * 52 + 4);
            }
            UpdateRows();
        }
    }
}
