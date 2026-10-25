using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace BlinkBuddy
{
    // Adding a mode for an app, in one place: pick the app (one that's open now, each with its icon and name, or a
    // program file found with Browse), pick its mode, Add.
    class AppRuleDialog : Form
    {
        readonly ListBox list;
        readonly ModeGrid modes;
        readonly RoundButton add, browse;
        readonly Dictionary<string, Bitmap> icons = new Dictionary<string, Bitmap>();
        readonly float scale;
        public Modes.Rule Result;

        [DllImport("uxtheme.dll", CharSet = CharSet.Unicode)] static extern int SetWindowTheme(IntPtr hwnd, string app, string idList);

        public AppRuleDialog(ICollection<string> taken)
        {
            using (Graphics g = Graphics.FromHwnd(IntPtr.Zero)) scale = g.DpiX / 96f;
            SuspendLayout();
            AutoScaleDimensions = new SizeF(96f, 96f);
            AutoScaleMode = AutoScaleMode.Dpi;
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = MinimizeBox = false;
            ShowInTaskbar = false;
            StartPosition = FormStartPosition.CenterParent;
            Text = Lang.T("Add an app");
            BackColor = Theme.Bg;
            ForeColor = Theme.Fg;
            Font = Theme.Body;
            ClientSize = new Size(440, 536);

            Controls.Add(Theme.MakeLabel("Choose an app", Theme.Heading, Theme.Fg, new Rectangle(20, 12, 400, 28)));
            Controls.Add(Theme.MakeLabel("Apps that are open now. Not in the list? Find its program with Browse.", Theme.Small, Theme.Dim,
                new Rectangle(20, 40, 400, 20)));
            list = new ListBox
            {
                IntegralHeight = false, BorderStyle = BorderStyle.None, DrawMode = DrawMode.OwnerDrawFixed,
                BackColor = Color.FromArgb(40, 40, 42), ForeColor = Theme.Fg, Bounds = new Rectangle(20, 66, 400, 236)
            };
            list.ItemHeight = (int)(44 * scale);
            list.DrawItem += DrawApp;
            list.SelectedIndexChanged += delegate { Check(); };
            list.DoubleClick += delegate { if (add.Enabled) Done(); };
            foreach (Modes.AppInfo a in Modes.RunningApps()) if (!taken.Contains(a.App)) list.Items.Add(a);
            Controls.Add(list);
            browse = new RoundButton { Text = "Browse...", Bounds = new Rectangle(20, 310, 120, 32) };
            browse.Click += delegate { Browse(); };
            Controls.Add(browse);

            Controls.Add(Theme.MakeLabel("Mode", Theme.Heading, Theme.Fg, new Rectangle(20, 356, 400, 28)));
            var names = new string[Modes.Custom];
            var glyphs = new string[Modes.Custom];
            Array.Copy(Modes.Names, names, Modes.Custom);
            Array.Copy(Modes.Glyphs, glyphs, Modes.Custom);
            modes = new ModeGrid(glyphs, names) { Bounds = new Rectangle(20, 388, 400, 88) };
            modes.SelectedChanged += delegate { Check(); };
            Controls.Add(modes);

            var cancel = new RoundButton { Text = "Cancel", Bounds = new Rectangle(204, 490, 104, 34) };
            cancel.Click += delegate { Close(); };
            add = new RoundButton { Text = "Add", Bounds = new Rectangle(316, 490, 104, 34), Enabled = false };   // (accent once it can be used)
            add.Click += delegate { if (add.Enabled) Done(); };
            Controls.Add(cancel);
            Controls.Add(add);
            ResumeLayout(false);
        }

        // The list's height, set once the window has its size for the display: scaling an owner-drawn list (whose rows are
        // set in pixels) let it grow over the Browse button and the modes under it.
        protected override void OnLoad(EventArgs e)
        {
            base.OnLoad(e);
            list.Height = browse.Top - (int)(8 * scale) - list.Top;
        }

        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            Native.DarkTitleBar(Handle);
            SetWindowTheme(list.Handle, "DarkMode_Explorer", null);   // a dark scroll bar
        }

        void Check()
        {
            add.Enabled = list.SelectedItem != null && modes.Selected >= 0;
            add.Tint = add.Enabled ? Theme.Accent : Color.Empty;
        }

        void Done()
        {
            var a = (Modes.AppInfo)list.SelectedItem;
            Result = new Modes.Rule { App = a.App, Mode = modes.Selected, Path = a.Path };
            DialogResult = DialogResult.OK;
            Close();
        }

        void Browse()
        {
            using (var d = new OpenFileDialog { Filter = Lang.T("Programs") + " (*.exe)|*.exe", Title = Lang.T("Add an app") })
            {
                if (d.ShowDialog(this) != DialogResult.OK) return;
                var a = new Modes.AppInfo { App = Path.GetFileName(d.FileName).ToLowerInvariant(), Title = "", Path = d.FileName };
                foreach (Modes.AppInfo x in list.Items) if (x.App == a.App) { list.SelectedItem = x; return; }
                list.Items.Insert(0, a);
                list.SelectedIndex = 0;
            }
        }

        // One app: its icon, its own name, and under that its file and window title.
        void DrawApp(object sender, DrawItemEventArgs e)
        {
            if (e.Index < 0) return;
            var a = (Modes.AppInfo)list.Items[e.Index];
            bool on = (e.State & DrawItemState.Selected) != 0;
            Color back = on ? Color.FromArgb(Theme.Bg.R + (Theme.Accent.R - Theme.Bg.R) / 4, Theme.Bg.G + (Theme.Accent.G - Theme.Bg.G) / 4,
                Theme.Bg.B + (Theme.Accent.B - Theme.Bg.B) / 4) : list.BackColor;
            using (var b = new SolidBrush(back)) e.Graphics.FillRectangle(b, e.Bounds);
            Rectangle r = e.Bounds;
            int icon = (int)(28 * scale), pad = (int)(10 * scale);
            Bitmap bmp;
            if (!icons.TryGetValue(a.App, out bmp)) icons[a.App] = bmp = Modes.IconOf(a.Path, icon);
            if (bmp != null) e.Graphics.DrawImage(bmp, r.X + pad, r.Y + (r.Height - icon) / 2, icon, icon);
            else TextRenderer.DrawText(e.Graphics, "", Theme.SectionIcons, new Rectangle(r.X + pad, r.Y, icon, r.Height), Theme.Dim,
                TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
            int x = r.X + pad * 2 + icon, half = r.Height / 2;
            TextRenderer.DrawText(e.Graphics, Modes.NameOf(a.App, a.Path), Theme.Body, new Rectangle(x, r.Y + 2, r.Right - x - pad, half), Theme.Fg,
                TextFormatFlags.Bottom | TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine | TextFormatFlags.NoPadding);
            string sub = a.Title.Length > 0 ? a.App + "  ·  " + a.Title : a.App;
            TextRenderer.DrawText(e.Graphics, sub, Theme.Small, new Rectangle(x, r.Y + half + 1, r.Right - x - pad, half - 2), Theme.Dim,
                TextFormatFlags.Top | TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine | TextFormatFlags.NoPadding);
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing) foreach (Bitmap b in icons.Values) if (b != null) b.Dispose();
            base.Dispose(disposing);
        }
    }
}
