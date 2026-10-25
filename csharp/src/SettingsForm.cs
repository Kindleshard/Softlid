using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Linq;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace BlinkBuddy
{
    // Created when opened and disposed when closed, so it costs nothing while you aren't looking at it.
    // Each tab is a scrolling stack of rows; rows hide and close up for the compact view and for options that are off.
    // This file has the window itself, its layout and the row builders; each page is built in its own file
    // (SettingsForm.Screen.cs, .Blink.cs, .Schedule.cs, .Extras.cs, .Options.cs).
    partial class SettingsForm : Form, IMessageFilter
    {
        const int W = 480, Edge = 456, H = 620;   // client width, right content edge and height, in 96-dpi units
        readonly App app;
        readonly Settings s;
        readonly Settings defaults = new Settings();
        readonly ScrollPage[] pages = new ScrollPage[5];
        readonly List<Stack> stacks = new List<Stack>();
        readonly List<RoundButton> miniButtons = new List<RoundButton>();   // both carry the mini mode symbol
        RoundButton titleMini;                                              // the one by the title bar (hidden when mini mode is off)
        readonly LangTips tips = new LangTips();
        readonly Timer clock = new Timer();
        Toggle filterToggle, hwToggle, stripToggle, grainToggle, toneToggle, invertToggle, coolToggle;
        Slider brightSlider, blueSlider, backlightSlider;
        ModeGrid modeGrid;
        IconPicker effectPicker;
        Label ruleStatus;
        Toggle awakeToggle;
        RoundButton focusButton;
        bool syncing;   // controls being set to match the app: not a change of your own
        Label hwStatus, toneStatus, lutStatus, nightStatus, syncStatus, syncTick, breakStatus, longStatus;
        CurveView curve;
        bool syncingBacklight;
        Action updateRows = delegate { };   // recomputes which rows show; each page adds its part

        [DllImport("user32.dll")] static extern IntPtr WindowFromPoint(Point p);

        public SettingsForm(App app)
        {
            this.app = app;
            s = app.S;
            SuspendLayout();
            AutoScaleDimensions = new SizeF(96f, 96f);
            AutoScaleMode = AutoScaleMode.Dpi;
            Text = "BlinkBuddy";
            Icon = app.WindowIcon;
            FormBorderStyle = FormBorderStyle.Sizable;   // taller or shorter only (see OnLoad and WndProc)
            MaximizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;
            BackColor = Theme.Bg;
            ForeColor = Theme.Fg;
            Font = Theme.Body;
            int height = s.SettingsHeight > 0 ? s.SettingsHeight : H;   // as tall as it was last left
            ClientSize = new Size(W, height);

            var tabs = tabStrip = new TabStrip("Screen", "Blink", "Schedule", "Extras", "") { Bounds = new Rectangle(8, 8, Edge - 44, 40) };
            tabs.SelectedChanged += delegate
            {
                for (int i = 0; i < pages.Length; i++) pages[i].Visible = i == tabs.Selected;
                UpdateRows();   // arranged again now it shows (laid out while hidden, it could be left a little off)
            };
            Controls.Add(tabs);
            tips.SetToolTip(tabs, "The cog holds BlinkBuddy's own options");
            var miniButton = titleMini = new RoundButton { Bounds = new Rectangle(Edge - 34, 12, 32, 32), Visible = s.MiniEnabled };
            miniButtons.Add(miniButton);
            tips.SetToolTip(miniButton, "Mini mode: shrink this window into a small icon that stays on top");
            miniButton.Click += delegate { app.EnterMiniMode(); };
            Controls.Add(miniButton);
            Controls.Add(new Panel { Bounds = new Rectangle(0, 52, W, 1), BackColor = Theme.Line });

            for (int i = 0; i < pages.Length; i++)
            {
                pages[i] = new ScrollPage { Bounds = new Rectangle(0, 54, W, height - 54), Visible = i == 0 };
                Controls.Add(pages[i]);
            }
            BuildScreenPage(new Stack(this, pages[0]));
            BuildBlinkPage(new Stack(this, pages[1]));
            BuildSchedulePage(new Stack(this, pages[2]));
            BuildExtrasPage(new Stack(this, pages[3]));
            BuildOptionsPage(new Stack(this, pages[4]));
            UpdateRows();
            ResumeLayout(false);

            clock.Interval = 1000;
            clock.Tick += delegate { RefreshStatus(); };
            clock.Start();
            Application.AddMessageFilter(this);
        }

        // ---- Layout: a page is a stack of rows, each a panel of fixed height; hidden rows take no space. ----

        class Row : Panel
        {
            public Func<bool> Shown = () => true;
        }

        class Stack
        {
            readonly SettingsForm form;
            readonly ScrollPage page;
            readonly List<Row> rows = new List<Row>();

            public Stack(SettingsForm form, ScrollPage page)
            {
                this.form = form;
                this.page = page;
                form.stacks.Add(this);
            }

            // Rows added while a section is open belong to it, and fold away with it (see SectionHead).
            Func<bool> section;
            public void Section(Func<bool> open) { section = open; }
            public void EndSection() { section = null; }

            public Row Add(int height, Func<bool> shown = null)
            {
                var r = new Row { Bounds = new Rectangle(0, 0, W, height), BackColor = Theme.Bg };
                Func<bool> own = shown ?? (() => true), open = section;
                r.Shown = open == null ? own : () => open() && own();
                page.Content.Controls.Add(r);
                rows.Add(r);
                return r;
            }

            // Rows that show or hide move the ones below; the page is drawn afresh after, so no row is left behind as a ghost
            // where it used to be (Windows would copy the old pixels along).
            public void Layout()
            {
                int y = 0;
                foreach (Row r in rows)
                {
                    bool show = r.Shown();
                    if (r.Visible != show) r.Visible = show;
                    if (!show) continue;
                    if (r.Top != y) r.Top = y;
                    y += r.Height;
                }
                page.Content.Height = y;
                page.Refit();
                page.Invalidate(true);   // the whole page, not just the rows: where they ended before (now below them) is the page's
            }
        }

        TabStrip tabStrip;

        // Diagnostics (--settings-shot): each tab, scrolled through a screen at a time, saved as tab-part.png in dir.
        public void Snapshot(string dir)
        {
            StartPosition = FormStartPosition.Manual;
            Location = new Point(-30000, 0);
            foreach (string key in new[] { "screen", "blink", "break", "stand", "strip", "mini" }) unfolded.Add(key);
            Show();
            UpdateRows();
            for (int i = 0; i < pages.Length; i++)
            {
                tabStrip.Selected = i;
                pages[i].ScrollBy(-1000000);
                for (int part = 0; part < 12; part++)
                {
                    Application.DoEvents();
                    using (var bmp = new Bitmap(Width, Height))
                    {
                        DrawToBitmap(bmp, new Rectangle(0, 0, Width, Height));
                        bmp.Save(System.IO.Path.Combine(dir, i + "-" + part + ".png"));
                    }
                    if (pages[i].AtEnd) break;
                    pages[i].ScrollBy(pages[i].Height * 3 / 4);
                }
            }
        }

        // Straight to the options page (the window is rebuilt there after the language changes).
        public void ShowOptions() { tabStrip.Selected = pages.Length - 1; }

        void UpdateRows()
        {
            updateRows();
            foreach (Stack st in stacks) st.Layout();
        }

        protected override void OnLoad(EventArgs e)
        {
            base.OnLoad(e);
            // Resizable in height only: its width is fixed, from a short window up to the screen's height.
            int frame = Height - ClientSize.Height;
            MinimumSize = new Size(Width, frame + 360 * ClientSize.Width / W);
            MaximumSize = new Size(Width, Math.Max(MinimumSize.Height, Screen.FromControl(this).WorkingArea.Height));
            FitPages();
            loaded = true;
            UpdateRows();   // again at the display's scale
            ShowRules();     // (its rows are placed by code: built again at the real scale)
            KeepLabelsClear();
            foreach (RoundButton b in miniButtons) b.Image = IconArt.MiniModeIcon((int)(b.Height * 0.62f));   // drawn at the real size
        }

        // The mouse wheel scrolls the page under the pointer; a slider takes it only once clicked into.
        public bool PreFilterMessage(ref Message m)
        {
            if (m.Msg != 0x20A) return false;   // WM_MOUSEWHEEL
            Control c = Control.FromChildHandle(WindowFromPoint(Cursor.Position));
            if (c == null || c.FindForm() != this) return false;
            if ((c is Slider || c is StepSlider) && c.Focused) return false;
            int delta = (short)((long)m.WParam >> 16);
            for (Control x = c; x != null; x = x.Parent)
            {
                var page = x as ScrollPage;
                if (page == null) continue;
                page.ScrollBy(-delta * pages[0].Height / 12 / 120);
                return true;
            }
            return false;
        }

        void RefreshStatus()
        {
            breakStatus.Text = app.BreakStatus();
            longStatus.Text = app.LongBreakStatus();
            nightStatus.Text = app.NightStatus();
            ShowSync();
            // a rule's mode in use, and the focus timer
            if (app.RuleMode >= 0) ruleStatus.Text = Lang.F("{0} mode, for the app in front", Lang.T(Modes.Names[app.RuleMode]));
            if (app.RuleMode != shownRule) { shownRule = app.RuleMode; UpdateRows(); }
            focusRing.Invalidate();
            focusButton.Text = app.FocusState == App.FocusWorking ? "Pause" : app.FocusState == App.FocusPaused ? "Resume" : "Start";
            hwStatus.Text = Lang.T(app.BacklightStatus);
            toneStatus.Text = lutStatus.Text = ToneText();
            curve.Invalidate();   // standard tone can adapt while the window is open
        }

        // Hotkeys and the mini window change settings while this window is open; mirror them.
        public void SyncFromApp()
        {
            syncing = true;
            filterToggle.Checked = s.FilterEnabled;
            toneToggle.Checked = s.ToneEnabled;
            brightSlider.Value = s.Brightness;
            coolToggle.Checked = s.CoolRange;
            blueSlider.SetRange(s.CoolRange ? -75 : 0, 100);
            blueSlider.Value = s.BlueFilter;
            stripToggle.Checked = s.StripEnabled;
            modeGrid.Selected = s.Mode;
            effectPicker.Selected = s.ColorEffect;
            awakeToggle.Checked = app.KeepingAwake;
            syncing = false;
            UpdateRows();
        }

        // The window's close button (or Alt+F4): ask, keep BlinkBuddy running in the tray, or exit, as set in Options.
        // Its minimise button: mini mode. (Closing it from code, e.g. for mini mode or a new language, isn't a close button and doesn't ask.)
        protected override void WndProc(ref Message m)
        {
            const int WM_SYSCOMMAND = 0x112, SC_CLOSE = 0xF060, SC_MINIMIZE = 0xF020;
            // Minimising folds the window into mini mode (when mini mode is on) instead of the taskbar.
            if (m.Msg == WM_SYSCOMMAND && ((int)m.WParam & 0xFFF0) == SC_MINIMIZE && s.MiniEnabled)
            {
                BeginInvoke((Action)app.EnterMiniMode);
                return;
            }
            if (m.Msg == 0x84)   // WM_NCHITTEST: the side edges and corners only resize up and down
            {
                base.WndProc(ref m);
                int hit = (int)m.Result;
                if (hit == 10 || hit == 11) m.Result = (IntPtr)18;        // left, right -> plain border
                else if (hit == 13 || hit == 14) m.Result = (IntPtr)12;   // top corners -> top
                else if (hit == 16 || hit == 17) m.Result = (IntPtr)15;   // bottom corners -> bottom
                return;
            }
            if (m.Msg == WM_SYSCOMMAND && ((int)m.WParam & 0xFFF0) == SC_CLOSE)
            {
                int choice = s.CloseAction;
                if (choice == 0)
                {
                    bool remember;
                    choice = CloseDialog.Ask(this, out remember);   // 0 cancelled, 1 tray, 2 exit
                    if (choice == 0) return;
                    if (remember) { s.CloseAction = choice; app.Changed(false); }
                }
                if (choice == 2) { app.Quit(); return; }
            }
            base.WndProc(ref m);
        }

        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            Native.DarkTitleBar(Handle);
        }

        // Once it is on screen, drawn afresh from top to bottom: the rows were arranged (and folded) while it was being
        // built and scaled, and a stale bit of that could otherwise stay showing until something redrew it.
        protected override void OnShown(EventArgs e)
        {
            base.OnShown(e);
            BeginInvoke((Action)delegate { if (!IsDisposed) Invalidate(true); });
        }

        // Taller or shorter: the pages follow, and the height is remembered (in 100%-scale units) once resizing ends.
        protected override void OnResize(EventArgs e)
        {
            base.OnResize(e);
            if (loaded) FitPages();   // (not while the window is being scaled for the display: the pages are scaled with it)
        }

        bool loaded;

        void FitPages() { foreach (ScrollPage page in pages) page.Height = Math.Max(1, ClientSize.Height - page.Top); }

        protected override void OnResizeEnd(EventArgs e)
        {
            base.OnResizeEnd(e);
            int height = ClientSize.Height * W / Math.Max(1, ClientSize.Width);
            if (Math.Abs(height - (s.SettingsHeight > 0 ? s.SettingsHeight : H)) < 2) return;
            s.SettingsHeight = height;
            app.Changed(false);
        }

        protected override void OnFormClosed(FormClosedEventArgs e)
        {
            Application.RemoveMessageFilter(this);
            clock.Dispose();
            tips.Dispose();
            base.OnFormClosed(e);
        }

        // Runs a after the window has a handle (built before it is shown).
        void BeginInvokeWhenReady(Action a)
        {
            if (IsHandleCreated) { Post(a); return; }
            EventHandler once = null;
            once = delegate { HandleCreated -= once; Post(a); };
            HandleCreated += once;
        }

        bool busyUpdating;

        // Runs on the UI thread, unless the window has gone.
        void Post(Action a)
        {
            if (IsDisposed || !IsHandleCreated) return;
            try { BeginInvoke(a); } catch (InvalidOperationException) { }
        }

        // ---- Row builders ----

        Label AddLabel(Control parent, string text, Font font, Color color, int x, int y, int w, int h)
        {
            Label l = Theme.MakeLabel(text, font, color, new Rectangle(x, y, w, h));
            parent.Controls.Add(l);
            return l;
        }

        // A section's heading with its switch: see SectionHead.
        Toggle Header(Stack st, string icon, string title, string hint, bool value, Action<bool> changed, Func<bool> shown = null)
        {
            Row r = SectionHead(st, icon, title, hint, shown);
            return AddToggle(r, Edge - 44, 13, value, changed);
        }

        // A section's heading: its icon (a Segoe Fluent Icons code point, in hex), title and one-line hint, with a
        // chevron that folds the section shut (clicking the title does too); the rows after it, up to the next divider,
        // belong to it. Folded sections are remembered. The chevron sits just left of where a switch goes.
        Row SectionHead(Stack st, string icon, string title, string hint, Func<bool> shown)
        {
            st.EndSection();
            bool two = LinesFor(hint, Theme.Small, Edge - 56) > 1;   // a longer translation gets a second line
            Row r = st.Add(two ? 82 : 64, shown);
            Title(r, icon, title);
            Label l = AddLabel(r, hint, Theme.Small, Theme.Dim, 56, 38, Edge - 56, two ? 38 : 20);
            if (two) l.TextAlign = ContentAlignment.TopLeft;
            var fold = new FoldButton(!IsFolded(title)) { Bounds = new Rectangle(Edge - 84, 13, 28, 28) };   // one column, switch or not
            tips.SetToolTip(fold, "Fold or unfold this section");
            Action toggle = delegate
            {
                SetFolded(title, !IsFolded(title));
                fold.Open = !IsFolded(title);
                UpdateRows();
            };
            fold.Click += delegate { toggle(); };
            foreach (Control c in r.Controls) { c.Cursor = Cursors.Hand; c.Click += delegate { toggle(); }; }
            r.Controls.Add(fold);
            st.Section(() => !IsFolded(title));
            return r;
        }

        bool IsFolded(string key) { return ("|" + s.Folded + "|").Contains("|" + key + "|"); }

        void SetFolded(string key, bool folded)
        {
            var keys = new List<string>(s.Folded.Split(new[] { '|' }, StringSplitOptions.RemoveEmptyEntries));
            keys.Remove(key);
            if (folded) keys.Add(key);
            s.Folded = string.Join("|", keys);
            app.Changed(false);
        }

        // A label reaching under the control beside it paints its background over that control's end (a dark block),
        // so any that do - a longer translation, say - are cut short (with an ellipsis) just before it.
        void KeepLabelsClear()
        {
            foreach (ScrollPage page in pages)
                foreach (Control row in page.Content.Controls)
                    foreach (Control c in row.Controls)
                    {
                        var l = c as Label;
                        if (l == null) continue;
                        foreach (Control other in row.Controls)
                        {
                            if (other == l || other is Label || other.Left <= l.Left) continue;
                            int overlap = Math.Min(l.Bottom, other.Bottom) - Math.Max(l.Top, other.Top);
                            if (overlap < Math.Min(l.Height, other.Height) / 2 || other.Left >= l.Right) continue;
                            l.Width = Math.Max(1, other.Left - l.Left - 4);
                            l.AutoEllipsis = true;
                        }
                    }
        }

        // How many lines a text takes (once translated) at a width in the window's own units.
        static readonly float screenScale = Graphics.FromHwnd(IntPtr.Zero).DpiX / 96f;
        static int LinesFor(string text, Font font, int width)
        {
            string t = Lang.T(text);
            int line = TextRenderer.MeasureText("Ag", font).Height;
            Size s = TextRenderer.MeasureText(t, font, new Size((int)(width * screenScale), int.MaxValue), TextFormatFlags.WordBreak | TextFormatFlags.NoPadding);
            return Math.Max(1, (int)Math.Ceiling(s.Height / (double)line - 0.1));
        }

        // The icon makes each section easy to spot while scrolling.
        void Title(Control r, string icon, string title)
        {
            Label glyph = AddLabel(r, ((char)Convert.ToInt32(icon, 16)).ToString(), Theme.SectionIcons, Theme.Accent, 20, 9, 32, 30);
            glyph.TextAlign = ContentAlignment.MiddleCenter;
            AddLabel(r, title, Theme.Heading, Theme.Fg, 56, 10, 300, 28);
        }

        // Folding: a section's less-used settings sit under "More options", folded away until it is opened (and they stay
        // open or folded while BlinkBuddy runs). The rows under it are shown with the predicate this returns. The Simple
        // view has no "More options" at all: just the essentials.
        static readonly HashSet<string> unfolded = new HashSet<string>();

        Func<bool> More(Stack st, string key, Func<bool> shown = null)
        {
            Row row;
            return More(st, key, shown, out row);
        }

        Func<bool> More(Stack st, string key, Func<bool> shown, out Row row)
        {
            Func<bool> visible = () => s.AdvancedView && (shown == null || shown());
            row = st.Add(36, visible);
            var d = new Disclosure(unfolded.Contains(key)) { Bounds = new Rectangle(20, 5, 220, 26) };
            d.Toggled += delegate { if (d.Open) unfolded.Add(key); else unfolded.Remove(key); UpdateRows(); };
            row.Controls.Add(d);
            return () => unfolded.Contains(key) && visible();
        }

        static Func<bool> And(Func<bool> a, Func<bool> b) { return () => a() && b(); }

        // A line between sections (it ends the one above).
        void Divider(Stack st, Func<bool> shown = null)
        {
            st.EndSection();
            st.Add(21, shown).Controls.Add(new Panel { Bounds = new Rectangle(24, 10, Edge - 24, 1), BackColor = Theme.Line });
        }

        void Note(Stack st, string text, int lines, Func<bool> shown = null)
        {
            lines = Math.Max(lines, LinesFor(text, Theme.Small, Edge - 24));   // as many as the translation needs
            Row r = st.Add(lines * 18 + 8, shown);
            Label l = AddLabel(r, text, Theme.Small, Theme.Dim, 24, 2, Edge - 24, lines * 18 + 4);
            l.TextAlign = ContentAlignment.TopLeft;
        }

        Toggle ToggleRow(Stack st, string text, bool value, Action<bool> changed, Func<bool> shown = null)
        {
            Row r = st.Add(34, shown);
            AddLabel(r, text, Theme.Body, Theme.Fg, 24, 4, 360, 26).AutoEllipsis = true;
            return AddToggle(r, Edge - 44, 6, value, changed);
        }

        Toggle AddToggle(Control parent, int x, int y, bool value, Action<bool> changed)
        {
            var t = new Toggle(value) { Location = new Point(x, y) };
            t.CheckedChanged += delegate { changed(t.Checked); };
            parent.Controls.Add(t);
            return t;
        }

        Slider SliderRow(Stack st, string text, int min, int max, int value, int def, Func<int, string> format, Action<int> changed,
            Func<bool> shown = null)
        {
            Row r = st.Add(62, shown);
            AddLabel(r, text, Theme.Body, Theme.Fg, 24, 4, 240, 22).AutoEllipsis = true;
            Label value_ = AddLabel(r, format(value), Theme.Body, Theme.Dim, 264, 4, 140, 22);   // (AddLabel translates)
            value_.TextAlign = ContentAlignment.MiddleRight;
            var slider = new Slider(min, max, value) { Bounds = new Rectangle(16, 28, 396, 30) };
            slider.ValueChanged += delegate { value_.Text = Lang.T(format(slider.Value)); changed(slider.Value); };
            r.Controls.Add(slider);
            Button reset = Theme.MakeButton("", new Rectangle(Edge - 30, 29, 28, 28));   // Segoe MDL2 "undo", a small round button
            reset.Font = Theme.Glyphs;
            reset.TabStop = false;
            tips.SetToolTip(reset, "Reset to default");
            reset.Click += delegate { slider.Value = def; };
            r.Controls.Add(reset);
            return slider;
        }
    }
}
