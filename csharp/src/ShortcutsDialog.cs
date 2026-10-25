using System;
using System.Drawing;
using System.Windows.Forms;

namespace BlinkBuddy
{
    // The keyboard shortcuts, each changeable: click its box and press the keys (with Ctrl, Alt and/or Shift);
    // Backspace or Delete leaves the action without one. Keys another program already uses are marked in red.
    // While this is open the shortcuts are off, so pressing one records it instead of doing it.
    class ShortcutsDialog : Form
    {
        readonly App app;
        readonly TextBox[] boxes = new TextBox[Hotkeys.Count + 1];
        readonly Label[] marks = new Label[Hotkeys.Count + 1];

        public ShortcutsDialog(App app)
        {
            this.app = app;
            SuspendLayout();
            AutoScaleDimensions = new SizeF(96f, 96f);
            AutoScaleMode = AutoScaleMode.Dpi;
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = MinimizeBox = false;
            ShowInTaskbar = false;
            StartPosition = FormStartPosition.CenterParent;
            Text = Lang.T("Keyboard shortcuts");
            BackColor = Theme.Bg;
            ForeColor = Theme.Fg;
            Font = Theme.Body;
            const int rowH = 36, top = 56;
            ClientSize = new Size(600, top + Hotkeys.Count * rowH + 64);
            Label intro = Theme.MakeLabel("Click a box and press the keys you want. Backspace removes a shortcut. The reading strip's " +
                "movement keys work while it stays put.", Theme.Small, Theme.Dim, new Rectangle(20, 10, 560, 40));
            intro.TextAlign = ContentAlignment.TopLeft;
            Controls.Add(intro);
            for (int id = 1; id <= Hotkeys.Count; id++)
            {
                int y = top + (id - 1) * rowH, action = id;
                Label name = Theme.MakeLabel(Hotkeys.Names[id - 1], Theme.Body, Theme.Fg, new Rectangle(20, y + 4, 300, 26));
                name.AutoEllipsis = true;
                Controls.Add(name);
                var box = new TextBox
                {
                    Bounds = new Rectangle(326, y + 3, 200, 28), Font = Theme.Body, ReadOnly = true, Cursor = Cursors.Hand,
                    BackColor = Color.FromArgb(45, 45, 45), ForeColor = Theme.Fg, BorderStyle = BorderStyle.FixedSingle,
                    TextAlign = HorizontalAlignment.Center, ShortcutsEnabled = false
                };
                box.KeyDown += (sender, e) => Record(action, e);
                box.GotFocus += delegate { box.BackColor = Color.FromArgb(38, 79, 112); };
                box.LostFocus += delegate { box.BackColor = Color.FromArgb(45, 45, 45); };
                boxes[id] = box;
                Controls.Add(box);
                Label mark = Theme.MakeLabel("In use", Theme.Small, Theme.Bad, new Rectangle(532, y + 6, 64, 22));
                marks[id] = mark;
                Controls.Add(mark);
            }
            int by = top + Hotkeys.Count * rowH + 14;
            var reset = new RoundButton { Text = "Reset all to default", Bounds = new Rectangle(20, by, 200, 34) };
            reset.Click += delegate { Hotkeys.ResetAll(app.S); Changed(); };
            var close = new RoundButton { Text = "Close", Bounds = new Rectangle(460, by, 120, 34), Tint = Theme.Accent };
            close.Click += delegate { Close(); };
            Controls.Add(reset);
            Controls.Add(close);
            ResumeLayout(false);
            Refresh2();
        }

        // Opens it over the settings window, with the shortcuts off meanwhile.
        public static void Open(App app, IWin32Window owner)
        {
            app.SuspendHotkeys();
            using (var d = new ShortcutsDialog(app)) d.ShowDialog(owner);
            app.RegisterHotkeys();
        }

        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            Native.DarkTitleBar(Handle);
        }

        void Record(int id, KeyEventArgs e)
        {
            e.SuppressKeyPress = true;
            e.Handled = true;
            if (e.KeyCode == Keys.Escape) { ActiveControl = null; return; }
            if (e.KeyCode == Keys.Back || e.KeyCode == Keys.Delete) { Hotkeys.Set(app.S, id, ""); Changed(); return; }
            string keys = Hotkeys.Describe(e.KeyData);
            if (keys.Length == 0) return;   // only modifiers so far
            Hotkeys.Set(app.S, id, keys);
            Changed();
        }

        // Saved, tried out (to see what's in use), and shown again.
        void Changed()
        {
            app.Changed(false);
            app.RegisterHotkeys();
            Refresh2();
            app.SuspendHotkeys();
        }

        void Refresh2()
        {
            for (int id = 1; id <= Hotkeys.Count; id++)
            {
                string keys = Hotkeys.Get(app.S, id);
                boxes[id].Text = keys.Length > 0 ? Hotkeys.Pretty(keys) : Lang.T("None");
                marks[id].Visible = app.HotkeyFailed(id);
            }
        }
    }
}
