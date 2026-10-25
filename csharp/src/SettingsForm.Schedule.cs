using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Linq;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace BlinkBuddy
{
    // The Schedule page: the night schedule and its place (synced, or a city you pick).
    partial class SettingsForm
    {
        // ---- Schedule: the blue light filter by the clock ----

        void BuildSchedulePage(Stack st)
        {
            Header(st, "E708", "Night schedule", "Eases the blue light filter in at night and back out in the morning.", s.ScheduleEnabled,
                v => { s.ScheduleEnabled = v; NightMarker(); NightChanged(); UpdateRows(); });
            Row nightPausedRow = st.Add(26, () => s.ScheduleEnabled && EffectPausesBlue());
            Label nightPaused = AddLabel(nightPausedRow, "Paused while a color effect is on", Theme.Small, Theme.Warn, 24, 0, Edge - 24, 22);
            updateRows += delegate
            {
                nightPaused.Text = Lang.F("Paused while {0} is on", Lang.T(EffectNames[Math.Max(0, Math.Min(EffectNames.Length - 1, s.ColorEffect))]));
            };
            // Sunset to sunrise, or hours of your own (the Advanced view only: Simple keeps to the sun)
            Func<bool> advanced = () => s.AdvancedView, hours = () => s.AdvancedView && s.ScheduleMode == 1;
            Row modeRow = st.Add(46, advanced);
            var mode = new Segmented("Sunset to sunrise", "Custom hours") { Bounds = new Rectangle(24, 4, Edge - 24, 34) };
            mode.Selected = s.ScheduleMode;
            modeRow.Controls.Add(mode);
            // The schedule's own strength: at night it takes over from the Blue light filter on the Screen tab.
            Slider night = SliderRow(st, "Blue light filter at night", 0, 100, s.NightBlue, defaults.NightBlue,
                v => v == 0 ? "Off" : ScreenFilter.KelvinFor(v) + " K",
                v => { s.NightBlue = v; app.PreviewNightStrength(); NightMarker(); NightChanged(); });
            night.Warm = true;
            Note(st, "Sunset and sunrise come from your location in Windows when it's allowed, and from your time zone otherwise.", 2,
                () => s.AdvancedView && s.ScheduleMode == 0);
            SliderRow(st, "Turn on at", 0, 95, s.NightFrom / 15, defaults.NightFrom / 15, v => Night.Hm(v * 15),
                v => { s.NightFrom = v * 15; NightChanged(); }, hours);
            SliderRow(st, "Turn off at", 0, 95, s.NightTo / 15, defaults.NightTo / 15, v => Night.Hm(v * 15),
                v => { s.NightTo = v * 15; NightChanged(); }, hours);
            SliderRow(st, "Ease in and out over", 0, 36, s.NightFade / 5, defaults.NightFade / 5,
                v => v == 0 ? "No easing" : v * 5 < 60 ? Lang.F("{0} min", v * 5) : Lang.F("{0} h {1:00} min", v * 5 / 60, v * 5 % 60),
                v => { s.NightFade = v * 5; NightChanged(); }, advanced);
            Note(st, "It starts easing in at the start time and is fully back to normal at the end time.", 1, advanced);
            Row statusRow = st.Add(70);
            nightStatus = AddLabel(statusRow, app.NightStatus(), Theme.Body, Theme.Dim, 24, 4, Edge - 24, 62);
            nightStatus.TextAlign = ContentAlignment.TopLeft;
            // When (and with what) the sun times were last synced, in green: Windows location, or the time zone's city.
            Row syncRow = st.Add(28, () => s.ScheduleEnabled && s.ScheduleMode == 0);
            syncTick = AddLabel(syncRow, "", Theme.Glyphs, Theme.Good, 22, 2, 22, 22);   // a check mark
            syncTick.TextAlign = ContentAlignment.MiddleCenter;
            syncStatus = AddLabel(syncRow, "Syncing with your location...", Theme.Small, Theme.Dim, 46, 2, Edge - 46, 22);
            BuildCityPicker(st);
            mode.SelectedChanged += delegate
            {
                s.ScheduleMode = mode.Selected;
                UpdateRows();
                NightChanged();
            };
            updateRows += delegate { mode.Selected = s.ScheduleMode; };   // (Simple view puts it back to the sun)
        }

        // ---- The schedule's place: synced by itself, or a city you pick ----

        TextBox cityBox;
        ListBox cityList;
        Row suggestRow;
        bool pickerOpen, choosingCity;
        int shownSync = -1, shownRule = -1;

        // The line under the status: green with a check when synced (or when you chose the city), red with a mark
        // when it couldn't sync, grey while syncing. A change in that state shows or hides the city picker.
        void ShowSync()
        {
            string text;
            int state = app.SyncState(out text);
            syncStatus.Text = text;
            syncStatus.ForeColor = state == App.SyncFailed ? Theme.Bad : state == App.Syncing ? Theme.Dim : Theme.Good;
            syncTick.Text = state == App.SyncFailed ? "" : "";   // an error badge, or a check mark
            syncTick.ForeColor = state == App.SyncFailed ? Theme.Bad : Theme.Good;
            syncTick.Visible = state != App.Syncing;
            if (state == shownSync) return;
            shownSync = state;
            UpdateRows();
        }

        // When syncing fails: a link to pick the city by hand; then a box to type it in, with matching cities listed
        // under it (dark, like the rest), and "Automatic" to go back to syncing by itself.
        void BuildCityPicker(Stack st)
        {
            Func<bool> sunset = () => s.ScheduleEnabled && s.ScheduleMode == 0;
            Row linkRow = st.Add(26, () => sunset() && shownSync == App.SyncFailed && !pickerOpen && !s.CityManual);
            var link = new LinkLabel
            {
                Text = Lang.T("Select your city manually"), Bounds = new Rectangle(46, 0, Edge - 46, 22), Font = Theme.Small,
                LinkColor = Theme.Accent, ActiveLinkColor = Theme.Fg, VisitedLinkColor = Theme.Accent, LinkBehavior = LinkBehavior.HoverUnderline,
                BackColor = Color.Transparent, TextAlign = ContentAlignment.MiddleLeft
            };
            link.LinkClicked += delegate { pickerOpen = true; UpdateRows(); cityBox.Focus(); };
            linkRow.Controls.Add(link);

            Row pickRow = st.Add(46, () => sunset() && (pickerOpen || s.CityManual));
            cityBox = new TextBox
            {
                Bounds = new Rectangle(24, 6, Edge - 24 - 132, 28), Font = Theme.Body, Text = s.CityManual ? s.City : "",
                BackColor = Color.FromArgb(45, 45, 45), ForeColor = Theme.Fg, BorderStyle = BorderStyle.FixedSingle
            };
            cityBox.HandleCreated += delegate { SendMessage(cityBox.Handle, 0x1501, (IntPtr)1, Lang.T("Type your city")); };   // EM_SETCUEBANNER
            pickRow.Controls.Add(cityBox);
            var auto = new RoundButton { Text = "Automatic", Bounds = new Rectangle(Edge - 120, 4, 120, 32) };
            auto.Click += delegate
            {
                pickerOpen = false;
                choosingCity = true;
                cityBox.Text = "";
                choosingCity = false;
                ShowSuggestions(new List<string>());
                app.SetCity("");
                RefreshStatus();
                UpdateRows();
            };
            pickRow.Controls.Add(auto);

            suggestRow = st.Add(40, () => sunset() && (pickerOpen || s.CityManual) && cityList.Items.Count > 0);
            cityList = new ListBox
            {
                Bounds = new Rectangle(24, 0, Edge - 24 - 132, 34), DrawMode = DrawMode.OwnerDrawFixed, BorderStyle = BorderStyle.None,
                BackColor = Color.FromArgb(45, 45, 48), ForeColor = Theme.Fg, Font = Theme.Body, IntegralHeight = false
            };
            cityList.DrawItem += (sender, e) =>
            {
                if (e.Index < 0) return;
                bool on = (e.State & DrawItemState.Selected) != 0;
                using (var b = new SolidBrush(on ? Color.FromArgb(38, 79, 112) : cityList.BackColor)) e.Graphics.FillRectangle(b, e.Bounds);
                var r = new Rectangle(e.Bounds.X + (int)(10 * screenScale), e.Bounds.Y, e.Bounds.Width - (int)(10 * screenScale), e.Bounds.Height);
                TextRenderer.DrawText(e.Graphics, (string)cityList.Items[e.Index], cityList.Font, r, Theme.Fg,
                    TextFormatFlags.VerticalCenter | TextFormatFlags.Left | TextFormatFlags.SingleLine);
            };
            cityList.MouseMove += (sender, e) =>
            {
                int i = cityList.IndexFromPoint(e.Location);
                if (i >= 0 && i != cityList.SelectedIndex) cityList.SelectedIndex = i;
            };
            cityList.MouseClick += (sender, e) =>
            {
                int i = cityList.IndexFromPoint(e.Location);
                if (i >= 0) ChooseCity((string)cityList.Items[i]);
            };
            suggestRow.Controls.Add(cityList);

            cityBox.TextChanged += delegate
            {
                if (choosingCity) return;
                ShowSuggestions(Night.FindCities(cityBox.Text, 6));
            };
            cityBox.KeyDown += (sender, e) =>
            {
                int n = cityList.Items.Count;
                if (e.KeyCode == Keys.Down && n > 0) { cityList.SelectedIndex = Math.Min(n - 1, cityList.SelectedIndex + 1); e.SuppressKeyPress = true; }
                else if (e.KeyCode == Keys.Up && n > 0) { cityList.SelectedIndex = Math.Max(0, cityList.SelectedIndex - 1); e.SuppressKeyPress = true; }
                else if (e.KeyCode == Keys.Enter)
                {
                    if (n > 0) ChooseCity((string)cityList.Items[Math.Max(0, cityList.SelectedIndex)]);
                    e.SuppressKeyPress = true;
                }
                else if (e.KeyCode == Keys.Escape && n > 0) { ShowSuggestions(new List<string>()); e.SuppressKeyPress = true; }
            };
        }

        // The list under the box, sized to its matches (or gone when there are none).
        void ShowSuggestions(List<string> names)
        {
            cityList.BeginUpdate();
            cityList.Items.Clear();
            foreach (string n in names) cityList.Items.Add(n);
            cityList.EndUpdate();
            if (names.Count > 0)
            {
                cityList.ItemHeight = (int)(26 * screenScale);
                cityList.Height = names.Count * cityList.ItemHeight + 2;
                cityList.SelectedIndex = 0;
                suggestRow.Height = cityList.Bottom + (int)(6 * screenScale);
            }
            UpdateRows();
        }

        void ChooseCity(string name)
        {
            choosingCity = true;
            cityBox.Text = name;
            cityBox.SelectionStart = name.Length;
            choosingCity = false;
            ShowSuggestions(new List<string>());
            app.SetCity(name);
            RefreshStatus();
            UpdateRows();
        }

        [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr SendMessage(IntPtr h, int msg, IntPtr w, string l);

        // The green mark on the Screen tab's blue light slider: where the night schedule takes the filter.
        void NightMarker() { blueSlider.Marker = s.ScheduleEnabled ? s.NightBlue : -1; }

        void NightChanged()
        {
            app.Changed(true);
            RefreshStatus();
        }
    }
}
