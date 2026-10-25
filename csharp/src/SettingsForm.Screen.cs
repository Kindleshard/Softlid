using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Linq;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace BlinkBuddy
{
    // The Screen page: the screen filter, its modes and colour effects, the backlight, and tone (with DesktopLUT).
    partial class SettingsForm
    {
        // ---- Screen: filter, brightness, warmth, backlight, then tone ----

        void BuildScreenPage(Stack st)
        {
            filterToggle = Header(st, "E706", "Screen filter", "Ctrl+Alt+Shift with F: on/off · ↑ ↓: brightness · ← →: warmth", s.FilterEnabled,
                v => { if (syncing) return; s.FilterEnabled = v; app.ManualScreenChange(); app.Changed(true); UpdateRows(); });
            // One-click modes; moving a slider makes it your own (Custom) again.
            Row modeRow = st.Add(88);
            modeGrid = new ModeGrid(Modes.Glyphs, Modes.Names) { Bounds = new Rectangle(24, 4, Edge - 24, 80), Selected = s.Mode };
            modeGrid.SelectedChanged += delegate { app.ChooseMode(modeGrid.Selected); };
            modeRow.Controls.Add(modeGrid);
            Row ruleRow = st.Add(26, () => app.RuleMode >= 0);
            ruleStatus = AddLabel(ruleRow, "A rule is in use.", Theme.Small, Theme.Accent, 24, 2, Edge - 24, 22);
            brightSlider = SliderRow(st, "Brightness", 20, 100, s.Brightness, defaults.Brightness, v => v + "%",
                v => { if (syncing) return; s.Brightness = v; app.ManualScreenChange(); FilterTouched(); });
            blueSlider = SliderRow(st, "Blue light filter", s.CoolRange ? -75 : 0, 100, s.BlueFilter, defaults.BlueFilter,
                v => v == 0 ? "Off" : ScreenFilter.KelvinFor(v) + " K",
                v =>
                {
                    if (syncing) return;
                    s.BlueFilter = v;
                    if (s.ColorEffect != ScreenFilter.NoEffect) { s.ColorEffect = ScreenFilter.NoEffect; ShowEffect(); }   // they take turns
                    app.ManualScreenChange();
                    app.PreviewNight();
                    FilterTouched();
                });
            blueSlider.Warm = true;
            blueSlider.Marker = s.ScheduleEnabled ? s.NightBlue : -1;   // where the night schedule takes it
            // A colour effect and the blue light filter take turns: while an effect is on, the slider is greyed and says so
            // (moving it brings the blue light filter back and the effect goes back to Original).
            Row pausedRow = st.Add(26, EffectPausesBlue);
            Label paused = AddLabel(pausedRow, "Paused while a color effect is on", Theme.Small, Theme.Dim, 24, 0, Edge - 24, 22);
            updateRows += delegate
            {
                blueSlider.Muted = EffectPausesBlue();
                paused.Text = Lang.F("Paused while {0} is on", Lang.T(EffectNames[Math.Max(0, Math.Min(EffectNames.Length - 1, s.ColorEffect))]));
            };
            // The real backlight: a switch, then a slider of its own; Brightness above always dims pixels.
            hwToggle = ToggleRow(st, "Dim with the display's real backlight", s.HardwareBrightness, OnBacklightToggled);
            backlightSlider = SliderRow(st, "Display backlight", 0, 100, Math.Max(0, s.Backlight), 100, v => v + "%",
                v => { s.Backlight = v; app.Changed(true); }, () => s.HardwareBrightness);
            Row hwRow = st.Add(26, () => s.HardwareBrightness || app.BacklightStatus.Length > 0);
            hwStatus = AddLabel(hwRow, app.BacklightStatus, Theme.Small, Theme.Dim, 24, 2, Edge - 24, 20);
            BuildEffects(st);

            Func<bool> more = More(st, "screen"), advanced = And(more, () => s.AdvancedView);
            coolToggle = ToggleRow(st, "Allow cooler than daylight (up to 10 000 K)", s.CoolRange, v =>
            {
                if (syncing) return;
                s.CoolRange = v;
                if (!v && s.BlueFilter < 0) s.BlueFilter = 0;
                syncing = true;   // the slider following the new range isn't a change of your own
                blueSlider.SetRange(v ? -75 : 0, 100);
                syncing = false;
                app.Changed(true);
            }, advanced);
            invertToggle = ToggleRow(st, "Invert colors", s.Invert, v => { s.Invert = v; FilterTouched(); }, advanced);

            Divider(st);
            BuildTone(st);
        }

        // ---- Colour effects: each shown as a little picture through it ----

        static readonly string[] EffectNames = { "Original", "Grayscale", "Sepia", "E-ink", "Red night" };
        Bitmap effectScene;
        readonly Bitmap[] effectShots = new Bitmap[5];
        int effectShotsStyle = -1;

        void BuildEffects(Stack st)
        {
            Row row = st.Add(80);   // (with room above it, apart from the sliders)
            AddLabel(row, "Color effect", Theme.Body, Theme.Fg, 24, 34, 124, 26);
            effectPicker = new IconPicker(5, s.ColorEffect, () => 1f, () => 0f) { Bounds = new Rectangle(150, 22, 5 * 48 + 4 * 13 + 2, 48) };
            effectPicker.Painter = PaintEffect;
            effectPicker.SelectedChanged += delegate
            {
                if (syncing) return;
                s.ColorEffect = effectPicker.Selected;
                app.ManualScreenChange();
                FilterTouched();
                UpdateRows();
            };
            effectPicker.HoverChanged += delegate { tips.SetToolTip(effectPicker, effectPicker.Hovered >= 0 ? EffectNames[effectPicker.Hovered] : ""); };
            row.Controls.Add(effectPicker);
            Row styleRow = st.Add(46, () => s.AdvancedView && s.ColorEffect == ScreenFilter.Grayscale);
            AddLabel(styleRow, "Grayscale", Theme.Body, Theme.Fg, 24, 10, 124, 26);
            var style = new Segmented("Rec.709", "Rec.601", "Black & white") { Bounds = new Rectangle(150, 6, Edge - 150, 34) };
            style.Selected = s.GrayStyle;
            style.SelectedChanged += delegate { s.GrayStyle = style.Selected; FilterTouched(); effectPicker.Invalidate(); };
            styleRow.Controls.Add(style);
            Note(st, "Color effects need the Standard, Smart layer or DesktopLUT engine; Smart (GPU) doesn't show them.", 2,
                () => s.FilterEnabled && !app.EffectsAvailable);
            updateRows += delegate { effectPicker.Enabled = app.EffectsAvailable; };
        }

        // Whether a colour effect is taking the blue light filter's place right now.
        bool EffectPausesBlue() { return s.FilterEnabled && s.ColorEffect != ScreenFilter.NoEffect && app.EffectsAvailable; }

        // The effect tiles following the settings (e.g. after the blue light slider put the effect back to Original).
        void ShowEffect()
        {
            syncing = true;
            effectPicker.Selected = s.ColorEffect;
            syncing = false;
            UpdateRows();
        }

        // A small scene (sky, sun, hill, a red flower, lines of text) as it looks through effect i.
        void PaintEffect(Graphics g, RectangleF r, int i, float hover)
        {
            if (effectShotsStyle != s.GrayStyle)
            {
                effectShotsStyle = s.GrayStyle;
                for (int k = 0; k < effectShots.Length; k++) if (effectShots[k] != null) { effectShots[k].Dispose(); effectShots[k] = null; }
            }
            if (effectScene == null) effectScene = EffectScene(64);
            if (effectShots[i] == null)
            {
                var shot = new Bitmap(effectScene.Width, effectScene.Height);
                for (int y = 0; y < shot.Height; y++)
                    for (int x = 0; x < shot.Width; x++) shot.SetPixel(x, y, ScreenFilter.Through(effectScene.GetPixel(x, y), i, s.GrayStyle));
                effectShots[i] = shot;
            }
            using (GraphicsPath p = IconArt.RoundRect(r, r.Height * 0.18f))
            {
                Region old = g.Clip;
                g.SetClip(p, CombineMode.Intersect);
                g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                g.DrawImage(effectShots[i], r);
                g.Clip = old;
            }
        }

        static Bitmap EffectScene(int n)
        {
            var b = new Bitmap(n, n);
            using (Graphics g = Graphics.FromImage(b))
            {
                g.SmoothingMode = SmoothingMode.AntiAlias;
                using (var sky = new LinearGradientBrush(new Point(0, 0), new Point(0, n), Color.FromArgb(70, 140, 230), Color.FromArgb(250, 170, 110)))
                    g.FillRectangle(sky, 0, 0, n, n);
                using (var sun = new SolidBrush(Color.FromArgb(255, 230, 120))) g.FillEllipse(sun, n * 0.58f, n * 0.14f, n * 0.24f, n * 0.24f);
                using (var hill = new SolidBrush(Color.FromArgb(70, 160, 80))) g.FillEllipse(hill, -n * 0.3f, n * 0.62f, n * 1.6f, n * 0.8f);
                using (var flower = new SolidBrush(Color.FromArgb(225, 50, 60))) g.FillEllipse(flower, n * 0.2f, n * 0.66f, n * 0.14f, n * 0.14f);
                using (var page = new SolidBrush(Color.FromArgb(245, 245, 240))) g.FillRectangle(page, n * 0.08f, n * 0.12f, n * 0.38f, n * 0.34f);
                using (var ink = new SolidBrush(Color.FromArgb(40, 40, 50)))
                    for (int k = 0; k < 4; k++) g.FillRectangle(ink, n * 0.12f, n * (0.17f + k * 0.07f), n * (k == 3 ? 0.18f : 0.3f), n * 0.025f);
            }
            return b;
        }

        // Two separate questions: which engine does the work (Standard, or an experimental one, each trading something),
        // and how strong. A strength means about the same white level in every engine; Custom (the three curve sliders)
        // exists only for Standard. The Simple view offers Standard at Gentle to Strong, without the curve.
        void BuildTone(Stack st)
        {
            Func<bool> advanced = () => s.AdvancedView;   // the engines and the curve: Advanced only
            toneToggle = Header(st, "E793", "Tone", "Calms bright areas and deepens darks, on top of Brightness.", s.ToneEnabled, v =>
            {
                if (syncing) { curve.Invalidate(); return; }
                s.ToneEnabled = v;
                app.ManualScreenChange();   // tone is part of a mode (Editing turns it off)
                if (v && !s.FilterEnabled) filterToggle.Checked = true;   // its handler applies everything
                else app.Changed(true);
                curve.Invalidate();
                RefreshStatus();
            });

            Row engineHead = st.Add(32, advanced);
            AddLabel(engineHead, "Engine", Theme.Small, Theme.Dim, 24, 8, 200, 18);
            Button labButton = Theme.MakeButton("Test lab...", new Rectangle(Edge - 96, 2, 96, 26));
            labButton.Font = Theme.Small;
            tips.SetToolTip(labButton, "Compare every engine and strength on a test picture and rate them");
            labButton.Click += delegate { app.OpenToneLab(); };
            engineHead.Controls.Add(labButton);

            var engines = new[]
            {
                new ChoiceRow("Standard", "No delay, no extra work, works everywhere.", null),
                new ChoiceRow("Smart (GPU)", "Best picture; about one frame of delay, a few watts.", "Experimental"),
                new ChoiceRow("Smart layer", "Lighter on power; can leave faint trails when moving.", "Experimental"),
                new ChoiceRow("DesktopLUT", "No delay, hue-true; needs DesktopLUT and admin rights.", "Experimental")
            };
            int[] engineMode = { 0, 2, 3, 4 };
            for (int i = 0; i < engines.Length; i++)
            {
                int mode = engineMode[i];
                if (i == 1)
                {
                    Row caption = st.Add(24, advanced);
                    AddLabel(caption, "Experimental: each one trades something", Theme.Small, Theme.Dim, 24, 4, Edge - 24, 18);
                }
                Row r = st.Add(58, advanced);
                engines[i].Bounds = new Rectangle(16, 2, Edge - 8, 54);
                r.Controls.Add(engines[i]);
                if (mode == 4) BuildLutSetup(st);
            }
            tips.SetToolTip(engines[1], "Looks at what surrounds every pixel: big bright areas are calmed while text, edges and colours keep their contrast.");
            tips.SetToolTip(engines[2], "The smart filter's dimming as one layer over the screen; it redraws only when something changed.");
            tips.SetToolTip(engines[3], "A free program that applies a colour table inside Windows. BlinkBuddy guards it: a 15-second check, " +
                "a crash watchdog and the panic key Ctrl+Alt+Shift+L all put the screen back.");

            Row strengthHead = st.Add(26);
            AddLabel(strengthHead, "Strength", Theme.Small, Theme.Dim, 24, 6, 200, 18);
            Row strengthRow = st.Add(56);
            var strength = new StepSlider(Comfort.Levels) { Bounds = new Rectangle(16, 2, Edge - 16 - 100, 50) };
            strengthRow.Controls.Add(strength);
            Button customButton = Theme.MakeButton("Custom", new Rectangle(Edge - 84, 8, 84, 30));
            customButton.Font = Theme.Small;
            tips.SetToolTip(customButton, "Set the curve yourself with three sliders (Standard engine only)");
            strengthRow.Controls.Add(customButton);

            // Below the strength: the graph and status (and, for Standard, adapting to white screens) ...
            Row graphRow = st.Add(108, () => s.AdvancedView && !s.CustomTone && s.ToneMode != 4);
            curve = new CurveView
            {
                Bounds = new Rectangle(24, 4, 92, 92),
                Curve = x => s.CustomTone ? ToneMapper.Net(x, s.ShadowCrush, s.Squeeze, s.LimiterStrength)
                    : s.ToneMode == 0 ? app.ComfortNet(x)
                    : SmartFilter.Flat(s.Level, x)
            };
            tips.SetToolTip(curve, "Input brightness (left to right) against what you see (bottom to top)");
            graphRow.Controls.Add(curve);
            toneStatus = AddLabel(graphRow, ToneText(), Theme.Small, Theme.Dim, 128, 2, Edge - 128, 66);
            toneStatus.TextAlign = ContentAlignment.TopLeft;
            var adaptRow = new Panel { Bounds = new Rectangle(128, 70, W - 128, 30), BackColor = Theme.Bg };
            AddLabel(adaptRow, "Dim more when the screen is mostly white", Theme.Small, Theme.Fg, 0, 4, Edge - 128 - 50, 22);
            AddToggle(adaptRow, Edge - 128 - 44, 3, s.AdaptToContent, v => { s.AdaptToContent = v; ToneChanged(); });
            graphRow.Controls.Add(adaptRow);

            // ... or the custom sliders ...
            Row customRow = st.Add(106, () => s.CustomTone);
            AddCompactSlider(customRow, 2, "Darken shadows", s.ShadowCrush, v => v == 0 ? "Off" : v + "%",
                v => { s.ShadowCrush = v; ToneChanged(); });
            AddCompactSlider(customRow, 35, "Squeeze highlights", s.Squeeze, v => v == 0 ? "Off" : v + "%",
                v => { s.Squeeze = v; ToneChanged(); });
            AddCompactSlider(customRow, 68, "Limit whites", s.LimiterStrength,
                v => v == 0 ? "Off" : Lang.F("to {0:0}%", ToneMapper.CapFor(v) * 100),
                v => { s.LimiterStrength = v; ToneChanged(); });

            // ... or where DesktopLUT is, the 15-second check, and its status.
            Row lutRow = st.Add(108, () => s.ToneMode == 4);
            var lutPath = new TextBox
            {
                Bounds = new Rectangle(24, 4, Edge - 24 - 104, 28), Text = s.LutPath, Font = Theme.Body,
                BackColor = Color.FromArgb(45, 45, 45), ForeColor = Theme.Fg, BorderStyle = BorderStyle.FixedSingle
            };
            tips.SetToolTip(lutPath, "Where DesktopLUT.exe is");
            Action<string> usePath = path =>
            {
                path = path.Trim().Trim('"');
                if (path == s.LutPath) return;
                s.LutPath = path;
                app.RetestLut();   // a different program gets the 15-second check (and this saves the path)
                SyncFromApp();
                RefreshStatus();
            };
            lutPath.Leave += delegate { usePath(lutPath.Text); };
            lutPath.KeyDown += (sender, e) => { if (e.KeyCode == Keys.Enter) { usePath(lutPath.Text); e.SuppressKeyPress = true; } };
            lutRow.Controls.Add(lutPath);
            Button browse = Theme.MakeButton("Browse...", new Rectangle(Edge - 96, 3, 96, 30));
            browse.Click += delegate
            {
                using (var pick = new OpenFileDialog { Filter = "DesktopLUT|DesktopLUT.exe|Programs|*.exe", FileName = "DesktopLUT.exe" })
                {
                    try { pick.InitialDirectory = System.IO.Path.GetDirectoryName(s.LutPath); } catch { }
                    if (pick.ShowDialog(this) != DialogResult.OK) return;
                    lutPath.Text = pick.FileName;
                    usePath(pick.FileName);
                }
            };
            lutRow.Controls.Add(browse);
            Button retest = Theme.MakeButton("Run the 15-second check again", new Rectangle(Edge - 230, 42, 230, 30));
            retest.Click += delegate
            {
                app.RetestLut();
                SyncFromApp();
                RefreshStatus();
            };
            lutRow.Controls.Add(retest);
            lutStatus = AddLabel(lutRow, "Starting...", Theme.Small, Theme.Dim, 24, 40, Edge - 24 - 240, 64);
            lutStatus.TextAlign = ContentAlignment.TopLeft;
            st.Add(16);

            // Keeps the engine list, the strength steps and the Custom button in line with the settings.
            bool settingSteps = false;
            Action showTone = delegate
            {
                for (int i = 0; i < engines.Length; i++) engines[i].Checked = engineMode[i] == s.ToneMode;
                settingSteps = true;
                bool four = s.AdvancedView;
                strength.SetSteps(four ? Comfort.Levels : new[] { Comfort.Levels[0], Comfort.Levels[1], Comfort.Levels[2] });
                strength.Selected = s.CustomTone ? -1 : s.Level;
                settingSteps = false;
                customButton.Visible = s.ToneMode == 0 && s.AdvancedView;
                customButton.ForeColor = s.CustomTone ? Theme.Accent : Theme.Fg;
                customButton.FlatAppearance.BorderColor = s.CustomTone ? Theme.Accent : Color.FromArgb(75, 75, 75);
            };
            updateRows += showTone;
            for (int i = 0; i < engines.Length; i++)
            {
                int mode = engineMode[i];
                engines[i].Chosen += delegate
                {
                    if (s.ToneMode == mode) return;
                    if (mode == 4 && !LutInstalled)
                    {
                        // DesktopLUT is a separate download: offer to set it up first.
                        lutSetupOpen = true;
                        UpdateRows();
                        return;
                    }
                    app.SetToneMode(mode);   // also leaves Custom, which only Standard has
                    UpdateRows();
                    ToneChanged();
                };
            }
            strength.SelectedChanged += delegate
            {
                if (settingSteps || strength.Selected < 0) return;
                s.ComfortLevel = strength.Selected;
                UpdateRows();
                ToneChanged();
            };
            customButton.Click += delegate
            {
                if (s.CustomTone) return;
                s.ComfortLevel = Settings.CustomLevel;
                UpdateRows();
                ToneChanged();
            };
        }

        bool lutSetupOpen;
        bool LutInstalled { get { return System.IO.File.Exists(s.LutPath); } }

        // DesktopLUT is optional and separate: until it is set up, choosing it opens this instead of switching to it.
        void BuildLutSetup(Stack st)
        {
            Row r = st.Add(132, () => lutSetupOpen && !LutInstalled);
            Label text = AddLabel(r, Lang.F("DesktopLUT is a separate free program ({0}, about 1 MB). " +
                "BlinkBuddy can download its latest release and set it up in your user folder. It asks for administrator " +
                "permission when it starts, and BlinkBuddy guards it with a 15-second check.", LutSetup.Page.Replace("https://", "")),
                Theme.Small, Theme.Dim, 48, 2, Edge - 48, 54);
            text.TextAlign = ContentAlignment.TopLeft;
            Button install = Theme.MakeButton("Download and set up", new Rectangle(48, 60, 170, 32));
            Button pick = Theme.MakeButton("I have it...", new Rectangle(226, 60, 110, 32));
            Button later = Theme.MakeButton("Not now", new Rectangle(344, 60, Edge - 344, 32));
            Label status = AddLabel(r, "Nothing is downloaded until you choose.", Theme.Small, Theme.Dim, 48, 98, Edge - 48, 32);
            status.TextAlign = ContentAlignment.TopLeft;
            r.Controls.Add(install);
            r.Controls.Add(pick);
            r.Controls.Add(later);
            Action ready = delegate
            {
                lutSetupOpen = false;
                app.SetToneMode(4);   // starts it: the permission prompt and the 15-second check follow
                UpdateRows();
                ToneChanged();
            };
            install.Click += delegate
            {
                install.Enabled = pick.Enabled = false;
                LutSetup.Install(
                    message => BeginInvoke((Action)delegate { if (!IsDisposed) status.Text = message; }),
                    (path, message) => BeginInvoke((Action)delegate
                    {
                        if (IsDisposed) return;
                        status.Text = message;
                        install.Enabled = pick.Enabled = true;
                        if (path == null) return;
                        s.LutPath = path;
                        s.LutConfirmedBuild = "";   // a new copy gets the 15-second check
                        ready();
                    }));
            };
            pick.Click += delegate
            {
                using (var dialog = new OpenFileDialog { Filter = "DesktopLUT|DesktopLUT.exe", FileName = "DesktopLUT.exe" })
                {
                    if (dialog.ShowDialog(this) != DialogResult.OK) return;
                    s.LutPath = dialog.FileName;
                    s.LutConfirmedBuild = "";
                    ready();
                }
            };
            later.Click += delegate { lutSetupOpen = false; UpdateRows(); };
        }

        // A label, a slider and its value on one line, for the custom tone sliders.
        void AddCompactSlider(Control p, int y, string text, int value, Func<int, string> format, Action<int> changed)
        {
            AddLabel(p, text, Theme.Small, Theme.Fg, 24, y, 140, 30);
            Label shown = AddLabel(p, format(value), Theme.Small, Theme.Dim, Edge - 70, y, 70, 30);
            shown.TextAlign = ContentAlignment.MiddleRight;
            var slider = new Slider(0, 100, value) { Bounds = new Rectangle(164, y, Edge - 164 - 74, 30) };
            slider.ValueChanged += delegate { shown.Text = Lang.T(format(slider.Value)); changed(slider.Value); };
            p.Controls.Add(slider);
        }

        // A tone control does nothing while tone or the whole screen filter is off, so touching one switches them on.
        void ToneChanged()
        {
            if (!s.ToneEnabled) toneToggle.Checked = true;   // its handler applies (and switches the filter on)
            else FilterTouched();
            curve.Invalidate();
            RefreshStatus();
        }

        string ToneText()
        {
            string text = ToneDetail();
            return app.GuardNote.Length > 0 && s.FilterEnabled ? app.GuardNote + "\n" + text : text;
        }

        string ToneDetail()
        {
            if (!s.FilterEnabled) return Lang.T("The screen filter is off, so nothing is applied.");
            if (!s.ToneEnabled) return Lang.T("Tone is off. Brightness and the blue light filter still apply.");
            if (s.CustomTone)
            {
                bool flat = s.ShadowCrush == 0 && s.Squeeze == 0 && s.LimiterStrength == 0;
                return flat ? Lang.T("All three sliders are at zero, so nothing changes.") : Lang.T("Right now:") + "\n" + app.ToneStatus.Replace("    ", "\n");
            }
            if (s.ToneMode == 2) return app.SmartStatus + "\n" + Lang.T("Graph: a large flat area. Details keep their contrast.");
            if (s.ToneMode == 3) return (app.LayerStatus.Length > 0 ? app.LayerStatus : Lang.T("Measuring...")) + "\n" + Lang.T("Graph: a large flat area.");
            if (s.ToneMode == 4)
                return (app.LutStatus.Length > 0 ? app.LutStatus : Lang.T("Starting...")) +
                       (app.LutFailed ? " " + Lang.T("Standard is used meanwhile.") : "\n" + Lang.T("Panic key: Ctrl+Alt+Shift+L."));
            return app.ComfortStatus();
        }

        // Moving a filter control while the filter is off would do nothing visible, so switch it on.
        void FilterTouched()
        {
            if (!s.FilterEnabled) filterToggle.Checked = true;   // its handler applies the filter
            else app.Changed(true);
        }

        void OnBacklightToggled(bool on)
        {
            if (syncingBacklight) return;
            hwStatus.Text = on ? Lang.T("Checking your displays...") : "";
            UpdateRows();
            app.SetHardwareBrightness(on, delegate
            {
                if (IsDisposed) return;
                syncingBacklight = true;
                hwToggle.Checked = s.HardwareBrightness;   // stays off if no display answered
                syncingBacklight = false;
                hwStatus.Text = Lang.T(app.BacklightStatus);
                backlightSlider.Value = Math.Max(0, s.Backlight);
                UpdateRows();
            });
        }
    }
}
