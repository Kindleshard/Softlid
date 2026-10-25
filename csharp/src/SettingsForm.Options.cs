using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Linq;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace BlinkBuddy
{
    // The Options page (the cog): BlinkBuddy itself, mini mode and updates.
    partial class SettingsForm
    {
        // ---- Options (the cog): BlinkBuddy itself ----

        void BuildOptionsPage(Stack st)
        {
            Row head = st.Add(64);
            Title(head, "E713", "BlinkBuddy options");
            AddLabel(head, "How BlinkBuddy itself behaves.", Theme.Small, Theme.Dim, 56, 38, Edge - 56, 20);
            // The language: Windows' own, or one of the list (each named in its own words). The window is rebuilt in it.
            Row langRow = st.Add(46);
            AddLabel(langRow, "Language", Theme.Body, Theme.Fg, 24, 10, 120, 26);
            var names = new List<string> { "Same as Windows" };
            foreach (Lang.Info info in Lang.All) names.Add(info.Name);
            var language = new DropDown(names.ToArray()) { Bounds = new Rectangle(150, 6, Edge - 150, 34) };
            language.Selected = s.Language == "" ? 0 : Array.FindIndex(Lang.All, i => i.Code == s.Language) + 1;
            language.SelectedChanged += delegate { app.SetLanguage(language.Selected == 0 ? "" : Lang.All[language.Selected - 1].Code); };
            langRow.Controls.Add(language);

            ToggleRow(st, "Start with Windows", app.RunAtStartup, v => app.RunAtStartup = v);
            ToggleRow(st, "Pause reminders in full-screen apps", s.PauseInFullscreen, v => { s.PauseInFullscreen = v; app.Changed(false); });

            Row viewRow = st.Add(50);
            AddLabel(viewRow, "View", Theme.Body, Theme.Fg, 24, 12, 100, 26);
            var view = new Segmented("Simple", "Advanced") { Bounds = new Rectangle(150, 8, Edge - 150, 34) };
            view.Selected = s.AdvancedView ? 1 : 0;
            view.SelectedChanged += delegate
            {
                if (view.Selected == 1)
                {
                    s.AdvancedView = true;
                    app.Changed(false);
                }
                else app.UseCompactView();   // switches the advanced features off
                app.Rebuild();   // built again for the new view, every control showing the settings as they now are
            };
            viewRow.Controls.Add(view);
            Note(st, "Simple shows just the essentials, without \"More options\". It turns off Invert colors, Film grain, cooler than " +
                "daylight, custom night hours, forced breaks and keeping the PC awake; tone uses the Standard " +
                "engine at Gentle to Strong. Modes for apps are kept but pause.", 4);

            // The accent colour: Windows' own, or one picked here (its swatch opens the colour wheel). The window is
            // rebuilt in the new colour once the wheel closes.
            Row accentRow = st.Add(46);
            AddLabel(accentRow, "Accent color", Theme.Body, Theme.Fg, 24, 10, 120, 26);
            var accentFrom = new Segmented("Match Windows", "Custom") { Bounds = new Rectangle(150, 6, Edge - 150 - (s.AccentFromWindows ? 0 : 46), 34) };
            accentFrom.Selected = s.AccentFromWindows ? 0 : 1;
            accentFrom.SelectedChanged += delegate { app.SetAccent(accentFrom.Selected == 0, s.AccentColor); };
            accentRow.Controls.Add(accentFrom);
            if (!s.AccentFromWindows)
            {
                var accent = new ColorButton { Bounds = new Rectangle(Edge - 36, 6, 34, 34), Value = Theme.Accent };
                tips.SetToolTip(accent, "Choose a color");
                accent.ColorChosen += delegate { if (accent.Value.ToArgb() != Theme.Accent.ToArgb()) app.SetAccent(false, accent.Value.ToArgb()); };
                accentRow.Controls.Add(accent);
            }

            // Closing this window: ask each time, keep BlinkBuddy running in the tray, or exit (a drop-down: the choices are
            // too long to sit side by side).
            Row closeRow = st.Add(80);
            AddLabel(closeRow, "When this window is closed", Theme.Body, Theme.Fg, 24, 6, Edge - 24, 26);
            var close = new DropDown("Ask me", "Keep running in the tray", "Exit BlinkBuddy") { Bounds = new Rectangle(24, 38, Edge - 24, 34) };
            close.Selected = s.CloseAction;
            close.SelectedChanged += delegate { s.CloseAction = close.Selected; app.Changed(false); };
            closeRow.Controls.Add(close);

            // Keyboard shortcuts: each one changeable, in a window of its own.
            Row keysRow = st.Add(46, () => s.AdvancedView);
            AddLabel(keysRow, "Keyboard shortcuts", Theme.Body, Theme.Fg, 24, 10, 240, 26);
            var keysButton = new RoundButton { Text = "Change...", Bounds = new Rectangle(Edge - 176, 6, 176, 34) };
            keysButton.Click += delegate { ShortcutsDialog.Open(app, this); };
            keysRow.Controls.Add(keysButton);

            // Keeping the PC awake (the tray menu has it too, for an hour or two)
            Func<bool> advanced = () => s.AdvancedView;
            awakeToggle = ToggleRow(st, "Keep PC awake", app.KeepingAwake, v => { if (!syncing) app.SetKeepAwake(v); }, advanced);
            Note(st, "Windows won't go to sleep or turn the screen off. The tray menu can also keep it awake for 1 or 2 hours.", 2, advanced);

            BuildMiniOptions(st);
            BuildUpdates(st);

            Divider(st);
            Row resetRow = st.Add(46);
            AddLabel(resetRow, "Every setting back to how it started", Theme.Body, Theme.Fg, 24, 10, Edge - 176 - 32, 26);
            Button reset = Theme.MakeButton("Reset all settings", new Rectangle(Edge - 176, 6, 176, 34));
            reset.Click += delegate { app.ResetAll(); };
            resetRow.Controls.Add(reset);
            st.Add(24);   // room below the last row
        }

        // Mini mode: on or off (the button by the title bar switches to it), which icon, how it fades, and the light
        // that now and then runs round its edge.
        void BuildMiniOptions(Stack st)
        {
            Divider(st);
            Func<bool> on = () => s.MiniEnabled;
            Header(st, "E944", "Mini mode", "A small icon that stays on top of other windows.", s.MiniEnabled, v =>
            {
                app.SetMiniEnabled(v);
                titleMini.Visible = v;
                UpdateRows();
            });
            Note(st, "Switch to it with the button next to the title bar, or by minimising this window. Hover over it for quick " +
                "controls; click it to come back here.", 2, on);
            // The icons as previews (each named in its tooltip), drawn with the screen's current brightness and warmth.
            Row iconRow = st.Add(64, on);
            AddLabel(iconRow, "Icon", Theme.Body, Theme.Fg, 24, 18, 120, 26);
            string[] iconNames = { "Eye", "Lamp", "Sun", "Laptop" };
            var icon = new IconPicker(4, s.MiniIcon, () => s.FilterEnabled ? (s.Brightness - 20) / 80f : 1f, () => app.WarmthNow())
            {
                Bounds = new Rectangle(150, 6, 4 * 48 + 3 * 13 + 2, 48)
            };
            icon.SelectedChanged += delegate { s.MiniIcon = icon.Selected; app.Changed(false); };
            icon.HoverChanged += delegate { tips.SetToolTip(icon, icon.Hovered >= 0 ? iconNames[icon.Hovered] : ""); };
            iconRow.Controls.Add(icon);
            Note(st, "Eye: opens when you come near. Lamp: as bright as the screen. Sun: glows when you come near. Laptop: shows " +
                "the screen's brightness and the blue light filter.", 2, on);
            ToggleRow(st, "Show the time to the next break on the icon's rim", s.MiniCountdown, v => { s.MiniCountdown = v; app.Changed(false); }, on);
            Func<bool> more = More(st, "mini", on), glow = And(more, () => s.MiniOrbit);
            SliderRow(st, "Fade-out delay after hovering", 10, 1000, s.MiniFadeDelay, 500, v => Ms(v),
                v => { s.MiniFadeDelay = v; app.Changed(false); }, more);
            SliderRow(st, "Fade-out speed", 0, 200, s.MiniFadeTime, 150, v => v == 0 ? "Instant" : Ms(v),
                v => { s.MiniFadeTime = v; app.Changed(false); }, more);
            ToggleRow(st, "The icon breathes with a soft glow now and then", s.MiniOrbit, v => { s.MiniOrbit = v; app.Changed(false); UpdateRows(); }, more);
            SliderRow(st, "Every", 0, 200, s.MiniOrbitSeconds, 60, v => v == 0 ? "Continuous" : Lang.F("{0} s", v),
                v => { s.MiniOrbitSeconds = v; app.Changed(false); }, glow);
            Row colorRow = st.Add(46, glow);
            AddLabel(colorRow, "Color", Theme.Body, Theme.Fg, 24, 10, 200, 26);
            var color = new ColorButton
            {
                Bounds = new Rectangle(Edge - 36, 6, 34, 34), AllowAccent = true, UseAccent = s.MiniOrbitColor == 0,
                Value = s.MiniOrbitColor == 0 ? Theme.Accent : Theme.Rgb(s.MiniOrbitColor), Presets = Theme.OrbitColors
            };
            tips.SetToolTip(color, "Choose a color");
            color.ColorChanged += delegate { s.MiniOrbitColor = color.UseAccent ? 0 : color.Value.ToArgb(); app.Changed(false); };
            colorRow.Controls.Add(color);
        }

        static string Ms(int v) { return v >= 1000 ? "1 s" : v + " ms"; }

        // Updates: this version and, under it, how things stand: green with a thumbs-up when up to date; orange when a
        // newer version is out, and the button turns into an orange "Update" that downloads it (with a progress bar),
        // puts it in place and restarts - nothing to set up. A quiet check runs when this page is built.
        void BuildUpdates(Stack st)
        {
            Divider(st);
            Row row = st.Add(46);
            AddLabel(row, Lang.F("Version {0}", Updater.CurrentText), Theme.Body, Theme.Fg, 24, 10, 220, 26);
            var button = new RoundButton { Text = "Check for updates", Bounds = new Rectangle(Edge - 176, 6, 176, 34) };
            row.Controls.Add(button);
            Row statusRow = st.Add(28);
            Label mark = AddLabel(statusRow, "", Theme.Glyphs, Theme.Good, 22, 2, 22, 22);   // thumbs up
            mark.TextAlign = ContentAlignment.MiddleCenter;
            Label status = AddLabel(statusRow, "Not checked yet.", Theme.Small, Theme.Dim, 46, 2, Edge - 46, 22);
            bool downloading = false;
            Row progressRow = st.Add(18, () => downloading);
            var progress = new ProgressLine { Bounds = new Rectangle(24, 5, Edge - 24, 6) };
            progressRow.Controls.Add(progress);

            // 0 neutral (dim), 1 up to date (green, thumbs up), 2 an update is out (orange, and the button says Update)
            Action<int, string> show = (kind, text) =>
            {
                status.Text = text;
                status.ForeColor = kind == 1 ? Theme.Good : kind == 2 ? Theme.Warn : Theme.Dim;
                mark.Visible = kind == 1;
                status.Left = kind == 1 ? mark.Right + 2 : mark.Left + 2;   // (again once scaled: see below)
                button.Tint = kind == 2 ? Theme.Warn : Color.Empty;
                button.Text = kind == 2 ? "Update" : "Check for updates";
            };
            Action check = delegate
            {
                busyUpdating = true;
                show(0, Lang.T("Checking..."));
                Updater.Check((release, message, current) => Post(delegate
                {
                    busyUpdating = false;
                    app.NewRelease = release;
                    if (release != null) show(2, Lang.F("Update available: version {0}", release.Tag));
                    else show(current ? 1 : 0, message);
                }));
            };
            if (app.NewRelease != null) show(2, Lang.F("Update available: version {0}", app.NewRelease.Tag));
            else if (Updater.Available) BeginInvokeWhenReady(check);
            else show(0, Lang.T("Updates aren't available yet: this copy of BlinkBuddy has no release channel."));

            button.Click += delegate
            {
                if (busyUpdating) return;
                if (app.NewRelease == null) { check(); return; }
                busyUpdating = downloading = true;
                progress.Progress = 0;
                UpdateRows();
                Updater.Install(app.NewRelease,
                    (fraction, text) => Post(delegate { progress.Progress = (float)fraction; show(2, text); }),
                    start => Post(delegate { app.Restart(start); }),
                    message => Post(delegate { busyUpdating = downloading = false; UpdateRows(); show(0, message); }));
            };
            updateRows += delegate { status.Left = mark.Visible ? mark.Right + 2 : mark.Left + 2; };   // placed at the display's scale
            ToggleRow(st, "Check for updates automatically (weekly)", s.UpdateCheck, v => { s.UpdateCheck = v; app.Changed(false); });
        }
    }
}
