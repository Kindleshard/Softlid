using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace BlinkBuddy
{
    // A colour, shown as a round swatch; clicking it opens the colour wheel pop-up. ColorChanged fires while you pick
    // (for a live preview), ColorChosen once the pop-up closes. With AllowAccent the pop-up also offers "Match accent",
    // and UseAccent then means the colour follows the accent (Value is ignored).
    class ColorButton : PaintedControl
    {
        Color value = Color.Black;
        bool hover, useAccent;
        public bool AllowAccent;
        public Color[] Presets = Theme.AccentColors;
        public event EventHandler ColorChanged, ColorChosen;

        public ColorButton()
        {
            Cursor = Cursors.Hand;
            SetStyle(ControlStyles.Selectable, true);
            TabStop = true;
        }

        public Color Value { get { return value; } set { this.value = value; Invalidate(); } }
        public bool UseAccent { get { return useAccent; } set { useAccent = value; Invalidate(); } }
        public Color Shown { get { return useAccent ? Theme.Accent : value; } }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            float d = Math.Min(Width, Height) - 2;
            var r = new RectangleF((Width - d) / 2f, (Height - d) / 2f, d, d);
            using (var ring = new Pen(hover || Focused ? Theme.Fg : Color.FromArgb(90, 90, 94), 1.5f)) g.DrawEllipse(ring, r);
            r.Inflate(-d * 0.14f, -d * 0.14f);
            using (var b = new SolidBrush(Shown)) g.FillEllipse(b, r);
            using (var pen = new Pen(Color.FromArgb(70, 255, 255, 255))) g.DrawEllipse(pen, r);   // so a dark colour still shows
        }

        protected override void OnMouseEnter(EventArgs e) { base.OnMouseEnter(e); hover = true; Invalidate(); }
        protected override void OnMouseLeave(EventArgs e) { base.OnMouseLeave(e); hover = false; Invalidate(); }
        protected override void OnGotFocus(EventArgs e) { base.OnGotFocus(e); Invalidate(); }
        protected override void OnLostFocus(EventArgs e) { base.OnLostFocus(e); Invalidate(); }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            base.OnKeyDown(e);
            if (e.KeyCode == Keys.Space || e.KeyCode == Keys.Enter) OnClick(EventArgs.Empty);
        }

        protected override void OnClick(EventArgs e)
        {
            base.OnClick(e);
            var popup = new ColorPopup(value, Presets, AllowAccent, useAccent);
            popup.Changed += delegate
            {
                value = popup.Value;
                useAccent = popup.MatchAccent;
                Invalidate();
                if (ColorChanged != null) ColorChanged(this, EventArgs.Empty);
            };
            popup.FormClosed += delegate { if (ColorChosen != null) ColorChosen(this, EventArgs.Empty); };
            // under the swatch, kept on the screen
            Point at = PointToScreen(new Point(Width / 2, Height + 4));
            Rectangle area = Screen.FromPoint(at).WorkingArea;
            popup.Location = new Point(Math.Max(area.Left, Math.Min(area.Right - popup.Width, at.X - popup.Width / 2)),
                at.Y + popup.Height <= area.Bottom ? at.Y : PointToScreen(Point.Empty).Y - popup.Height - 4);
            popup.Show(FindForm());
        }
    }

    // The colour pop-up: hue round the wheel and saturation out from its middle, brightness on the bar under it, quick
    // picks, and the colour as a hex code you can type. It closes when you click anywhere else.
    class ColorPopup : Form
    {
        double hue, sat, val;
        bool matchAccent, updating;
        readonly Wheel wheel;
        readonly ValueBar bar;
        readonly Swatches quick;
        readonly TextBox hex;
        readonly RoundButton accentButton;
        public event EventHandler Changed;

        public Color Value { get { return FromHsv(hue, sat, val); } }
        public bool MatchAccent { get { return matchAccent; } }

        public ColorPopup(Color start, Color[] presets, bool allowAccent, bool useAccent)
        {
            SuspendLayout();
            AutoScaleDimensions = new SizeF(96f, 96f);
            AutoScaleMode = AutoScaleMode.Dpi;
            FormBorderStyle = FormBorderStyle.None;
            ShowInTaskbar = false;
            StartPosition = FormStartPosition.Manual;
            KeyPreview = true;
            BackColor = Color.FromArgb(43, 43, 43);
            ClientSize = new Size(248, 344);
            matchAccent = useAccent;
            ToHsv(start, out hue, out sat, out val);

            wheel = new Wheel(this) { Bounds = new Rectangle(16, 16, 216, 216) };
            bar = new ValueBar(this) { Bounds = new Rectangle(16, 244, 216, 20) };
            quick = new Swatches(this, presets) { Bounds = new Rectangle(14, 274, 220, 26) };
            hex = new TextBox
            {
                Bounds = new Rectangle(16, 308, 96, 26), Font = Theme.Body, BorderStyle = BorderStyle.FixedSingle,
                BackColor = Color.FromArgb(32, 32, 32), ForeColor = Theme.Fg, MaxLength = 7, CharacterCasing = CharacterCasing.Upper
            };
            hex.TextChanged += delegate
            {
                if (updating) return;
                string t = hex.Text.TrimStart('#');
                int rgb;
                if (t.Length != 6 || !int.TryParse(t, System.Globalization.NumberStyles.HexNumber, null, out rgb)) return;
                Pick(Color.FromArgb(255, Color.FromArgb(rgb)), false);
            };
            Controls.Add(wheel);
            Controls.Add(bar);
            Controls.Add(quick);
            Controls.Add(hex);
            if (allowAccent)
            {
                accentButton = new RoundButton { Text = "Match accent", Bounds = new Rectangle(122, 305, 110, 32) };
                accentButton.Click += delegate { matchAccent = !matchAccent; if (matchAccent) ToHsv(Theme.Accent, out hue, out sat, out val); Redraw(true); };
                Controls.Add(accentButton);
            }
            ResumeLayout(false);
            Redraw(true);
        }

        protected override CreateParams CreateParams
        {
            get
            {
                CreateParams cp = base.CreateParams;
                cp.ClassStyle |= 0x20000;   // CS_DROPSHADOW
                return cp;
            }
        }

        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            Native.RoundCorners(Handle);
        }

        protected override void OnDeactivate(EventArgs e)
        {
            base.OnDeactivate(e);
            BeginInvoke((Action)Close);
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            base.OnKeyDown(e);
            if (e.KeyCode == Keys.Escape || e.KeyCode == Keys.Enter) Close();
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            base.OnPaint(e);
            using (var pen = new Pen(Color.FromArgb(72, 72, 72))) e.Graphics.DrawRectangle(pen, 0, 0, ClientSize.Width - 1, ClientSize.Height - 1);
        }

        // A colour chosen on the wheel, the bar, a quick pick or the hex box (which then isn't rewritten as you type).
        void Pick(Color c, bool writeHex)
        {
            ToHsv(c, out hue, out sat, out val);
            matchAccent = false;
            Redraw(writeHex);
        }

        void Redraw(bool writeHex)
        {
            wheel.Invalidate();
            bar.Invalidate();
            quick.Invalidate();
            if (accentButton != null) accentButton.Tint = matchAccent ? Theme.Accent : Color.Empty;
            if (writeHex)
            {
                updating = true;
                Color c = Value;
                hex.Text = string.Format("#{0:X2}{1:X2}{2:X2}", c.R, c.G, c.B);
                updating = false;
            }
            if (Changed != null) Changed(this, EventArgs.Empty);
        }

        public static Color FromHsv(double h, double s, double v)
        {
            h = ((h % 360) + 360) % 360 / 60;
            int i = (int)Math.Floor(h);
            double f = h - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
            double r, g, b;
            switch (i)
            {
                case 0: r = v; g = t; b = p; break;
                case 1: r = q; g = v; b = p; break;
                case 2: r = p; g = v; b = t; break;
                case 3: r = p; g = q; b = v; break;
                case 4: r = t; g = p; b = v; break;
                default: r = v; g = p; b = q; break;
            }
            return Color.FromArgb((int)Math.Round(r * 255), (int)Math.Round(g * 255), (int)Math.Round(b * 255));
        }

        public static void ToHsv(Color c, out double h, out double s, out double v)
        {
            double r = c.R / 255.0, g = c.G / 255.0, b = c.B / 255.0;
            double max = Math.Max(r, Math.Max(g, b)), min = Math.Min(r, Math.Min(g, b)), d = max - min;
            v = max;
            s = max <= 0 ? 0 : d / max;
            if (d <= 0) h = 0;
            else if (max == r) h = 60 * (((g - b) / d) % 6);
            else if (max == g) h = 60 * ((b - r) / d + 2);
            else h = 60 * ((r - g) / d + 4);
            if (h < 0) h += 360;
        }

        // Hue round, saturation outwards, drawn at the bar's brightness; a ring marks the colour.
        class Wheel : PaintedControl
        {
            readonly ColorPopup owner;
            Bitmap disc;

            public Wheel(ColorPopup owner) { this.owner = owner; BackColor = owner.BackColor; Cursor = Cursors.Cross; }

            float Radius { get { return Math.Min(Width, Height) / 2f - 1; } }

            protected override void OnPaint(PaintEventArgs e)
            {
                Graphics g = e.Graphics;
                g.SmoothingMode = SmoothingMode.AntiAlias;
                if (disc == null || disc.Width != Width || disc.Height != Height) Draw();
                g.DrawImage(disc, 0, 0);
                float R = Radius, cx = Width / 2f, cy = Height / 2f;
                if (owner.val < 1)
                    using (var shade = new SolidBrush(Color.FromArgb((int)((1 - owner.val) * 255), 0, 0, 0)))
                        g.FillEllipse(shade, cx - R, cy - R, 2 * R, 2 * R);
                double a = owner.hue * Math.PI / 180;
                float mx = cx + (float)(Math.Cos(a) * owner.sat * R), my = cy - (float)(Math.Sin(a) * owner.sat * R), m = Height * 0.045f;
                using (var dark = new Pen(Color.FromArgb(160, 0, 0, 0), 3f)) g.DrawEllipse(dark, mx - m, my - m, 2 * m, 2 * m);
                using (var light = new Pen(Color.White, 1.6f)) g.DrawEllipse(light, mx - m, my - m, 2 * m, 2 * m);
            }

            void Draw()
            {
                if (disc != null) disc.Dispose();
                int w = Width, h = Height;
                disc = new Bitmap(w, h, PixelFormat.Format32bppArgb);
                float R = Radius, cx = w / 2f, cy = h / 2f;
                BitmapData data = disc.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
                var row = new int[w];
                for (int y = 0; y < h; y++)
                {
                    for (int x = 0; x < w; x++)
                    {
                        double dx = x + 0.5 - cx, dy = cy - (y + 0.5), d = Math.Sqrt(dx * dx + dy * dy);
                        double alpha = Math.Max(0, Math.Min(1, R - d + 0.5));
                        if (alpha <= 0) { row[x] = 0; continue; }
                        double hue = Math.Atan2(dy, dx) * 180 / Math.PI;
                        Color c = FromHsv(hue, Math.Min(1, d / R), 1);
                        row[x] = ((int)(alpha * 255) << 24) | (c.R << 16) | (c.G << 8) | c.B;
                    }
                    Marshal.Copy(row, 0, data.Scan0 + y * data.Stride, w);
                }
                disc.UnlockBits(data);
            }

            void PickAt(Point p)
            {
                float R = Radius;
                double dx = p.X - Width / 2.0, dy = Height / 2.0 - p.Y;
                owner.hue = (Math.Atan2(dy, dx) * 180 / Math.PI + 360) % 360;
                owner.sat = Math.Min(1, Math.Sqrt(dx * dx + dy * dy) / R);
                if (owner.val < 0.05) owner.val = 1;   // picking a hue on a black wheel: show it
                owner.matchAccent = false;
                owner.Redraw(true);
            }

            protected override void OnMouseDown(MouseEventArgs e) { base.OnMouseDown(e); PickAt(e.Location); }
            protected override void OnMouseMove(MouseEventArgs e) { base.OnMouseMove(e); if (e.Button == MouseButtons.Left) PickAt(e.Location); }

            protected override void Dispose(bool disposing)
            {
                if (disposing && disc != null) disc.Dispose();
                base.Dispose(disposing);
            }
        }

        // Brightness: black on the left to the full colour on the right.
        class ValueBar : PaintedControl
        {
            readonly ColorPopup owner;

            public ValueBar(ColorPopup owner) { this.owner = owner; BackColor = owner.BackColor; Cursor = Cursors.Hand; }

            protected override void OnPaint(PaintEventArgs e)
            {
                Graphics g = e.Graphics;
                g.SmoothingMode = SmoothingMode.AntiAlias;
                float knob = Height - 2;
                var track = new RectangleF(knob / 2, Height * 0.2f, Width - knob, Height * 0.6f);
                using (GraphicsPath p = IconArt.RoundRect(track, track.Height / 2))
                using (var b = new LinearGradientBrush(new PointF(track.Left - 1, 0), new PointF(track.Right + 1, 0), Color.Black, FromHsv(owner.hue, owner.sat, 1)))
                    g.FillPath(b, p);
                float x = track.Left + (float)owner.val * track.Width;
                using (var fill = new SolidBrush(owner.Value)) g.FillEllipse(fill, x - knob / 2, 1, knob, knob);
                using (var pen = new Pen(Color.White, 1.6f)) g.DrawEllipse(pen, x - knob / 2, 1, knob, knob);
            }

            void PickAt(int px)
            {
                float knob = Height - 2;
                owner.val = Math.Max(0, Math.Min(1, (px - knob / 2) / (Width - knob)));
                owner.matchAccent = false;
                owner.Redraw(true);
            }

            protected override void OnMouseDown(MouseEventArgs e) { base.OnMouseDown(e); PickAt(e.X); }
            protected override void OnMouseMove(MouseEventArgs e) { base.OnMouseMove(e); if (e.Button == MouseButtons.Left) PickAt(e.X); }
        }

        // Quick picks in a row.
        class Swatches : PaintedControl
        {
            readonly ColorPopup owner;
            readonly Color[] colors;
            int hover = -1;

            public Swatches(ColorPopup owner, Color[] colors) { this.owner = owner; this.colors = colors; BackColor = owner.BackColor; Cursor = Cursors.Hand; }

            float Step { get { return Width / (float)Math.Max(1, colors.Length); } }

            protected override void OnPaint(PaintEventArgs e)
            {
                Graphics g = e.Graphics;
                g.SmoothingMode = SmoothingMode.AntiAlias;
                float d = Math.Min(Step, Height) - 6;
                Color now = owner.Value;
                for (int i = 0; i < colors.Length; i++)
                {
                    float cx = Step * (i + 0.5f), cy = Height / 2f;
                    using (var b = new SolidBrush(colors[i])) g.FillEllipse(b, cx - d / 2, cy - d / 2, d, d);
                    using (var pen = new Pen(Color.FromArgb(80, 255, 255, 255))) g.DrawEllipse(pen, cx - d / 2, cy - d / 2, d, d);
                    bool on = colors[i].R == now.R && colors[i].G == now.G && colors[i].B == now.B && !owner.matchAccent;
                    if (!on && i != hover) continue;
                    float rr = d / 2 + 2.5f;
                    using (var pen = new Pen(on ? Theme.Fg : Theme.Dim, 1.5f)) g.DrawEllipse(pen, cx - rr, cy - rr, 2 * rr, 2 * rr);
                }
            }

            protected override void OnMouseMove(MouseEventArgs e)
            {
                base.OnMouseMove(e);
                int i = Math.Min(colors.Length - 1, (int)(e.X / Step));
                if (i != hover) { hover = i; Invalidate(); }
            }

            protected override void OnMouseLeave(EventArgs e) { base.OnMouseLeave(e); hover = -1; Invalidate(); }

            protected override void OnMouseClick(MouseEventArgs e)
            {
                base.OnMouseClick(e);
                int i = (int)(e.X / Step);
                if (i >= 0 && i < colors.Length) owner.Pick(colors[i], true);
            }
        }
    }

    // A choice from a short list, as a pill showing the current one; clicking opens the list (in the app's dark menu).
    class DropDown : PaintedControl
    {
        string[] items;
        int selected;
        bool hover;
        public event EventHandler SelectedChanged;

        public DropDown(params string[] items)
        {
            this.items = Lang.T(items);
            Font = Theme.Body;
            Cursor = Cursors.Hand;
            SetStyle(ControlStyles.Selectable, true);
            TabStop = true;
        }

        public void SetItems(params string[] items) { this.items = Lang.T(items); Invalidate(); }

        public int Selected
        {
            get { return selected; }
            set
            {
                if (value == selected) return;
                selected = value;
                Invalidate();
                Raise(SelectedChanged, this);
            }
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            var r = new RectangleF(0.5f, 0.5f, Width - 1.5f, Height - 1.5f);
            using (GraphicsPath p = IconArt.RoundRect(r, r.Height / 2))
            {
                using (var b = new SolidBrush(hover ? Color.FromArgb(62, 62, 62) : Color.FromArgb(52, 52, 52))) g.FillPath(b, p);
                using (var pen = new Pen(Focused ? Theme.Accent : Color.FromArgb(75, 75, 75))) g.DrawPath(pen, p);
            }
            int pad = Height / 2, arrow = Height;
            string text = selected >= 0 && selected < items.Length ? items[selected] : "";
            TextRenderer.DrawText(g, text, Font, new Rectangle(pad, 0, Width - pad - arrow, Height), Theme.Fg,
                TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine | TextFormatFlags.NoPadding);
            TextRenderer.DrawText(g, "", Theme.Glyphs, new Rectangle(Width - arrow - 4, 0, arrow, Height), Theme.Dim,
                TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
        }

        protected override void OnMouseEnter(EventArgs e) { base.OnMouseEnter(e); hover = true; Invalidate(); }
        protected override void OnMouseLeave(EventArgs e) { base.OnMouseLeave(e); hover = false; Invalidate(); }
        protected override void OnGotFocus(EventArgs e) { base.OnGotFocus(e); Invalidate(); }
        protected override void OnLostFocus(EventArgs e) { base.OnLostFocus(e); Invalidate(); }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            base.OnKeyDown(e);
            if (e.KeyCode == Keys.Space || e.KeyCode == Keys.Enter) Open();
            if (e.KeyCode == Keys.Down && selected < items.Length - 1) Selected = selected + 1;
            if (e.KeyCode == Keys.Up && selected > 0) Selected = selected - 1;
        }

        protected override void OnMouseClick(MouseEventArgs e) { base.OnMouseClick(e); Open(); }

        void Open()
        {
            var menu = new ContextMenuStrip { Renderer = new DarkMenuRenderer(), Font = Font, MinimumSize = new Size(Width, 0) };
            menu.Opened += delegate { Native.RoundCorners(menu.Handle); };
            for (int i = 0; i < items.Length; i++)
            {
                int chosen = i;
                menu.Items.Add(new ToolStripMenuItem(items[i], null, delegate { Selected = chosen; }) { Checked = i == selected });
            }
            menu.Closed += delegate { BeginInvoke((Action)delegate { menu.Dispose(); }); };
            menu.Show(this, new Point(0, Height + 2));
        }
    }

    // "More options" under a section: a small link with a chevron that folds the less-used settings in and out.
    class Disclosure : PaintedControl
    {
        static readonly Font Chevron = new Font(Theme.GlyphFamily, 8f);
        bool open, hover;
        public event EventHandler Toggled;

        public Disclosure(bool open)
        {
            this.open = open;
            Font = Theme.Small;
            Cursor = Cursors.Hand;
            SetStyle(ControlStyles.Selectable, true);
            TabStop = true;
        }

        public bool Open { get { return open; } }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            Color ink = hover || Focused ? Theme.Fg : Theme.Accent;
            int glyph = Height;
            TextRenderer.DrawText(g, open ? "" : "", Chevron, new Rectangle(0, 0, glyph, Height), ink,
                TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
            TextRenderer.DrawText(g, Lang.T(open ? "Fewer options" : "More options"), Font, new Rectangle(glyph, 0, Width - glyph, Height), ink,
                TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine | TextFormatFlags.NoPadding);
        }

        protected override void OnMouseEnter(EventArgs e) { base.OnMouseEnter(e); hover = true; Invalidate(); }
        protected override void OnMouseLeave(EventArgs e) { base.OnMouseLeave(e); hover = false; Invalidate(); }
        protected override void OnGotFocus(EventArgs e) { base.OnGotFocus(e); Invalidate(); }
        protected override void OnLostFocus(EventArgs e) { base.OnLostFocus(e); Invalidate(); }
        protected override void OnMouseClick(MouseEventArgs e) { base.OnMouseClick(e); Toggle(); }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            base.OnKeyDown(e);
            if (e.KeyCode == Keys.Space || e.KeyCode == Keys.Enter) Toggle();
        }

        void Toggle()
        {
            open = !open;
            Invalidate();
            Raise(Toggled, this);
        }
    }

    // The chevron on a section's heading: up while the section is open (click to fold it), down while it is folded.
    class FoldButton : PaintedControl
    {
        bool open, hover;

        public FoldButton(bool open)
        {
            this.open = open;
            Cursor = Cursors.Hand;
        }

        public bool Open { get { return open; } set { if (value != open) { open = value; Invalidate(); } } }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            if (hover) using (var b = new SolidBrush(Color.FromArgb(52, 52, 54))) g.FillEllipse(b, 0, 0, Width - 1, Height - 1);
            TextRenderer.DrawText(g, open ? "" : "", Theme.Glyphs, ClientRectangle, hover ? Theme.Fg : Theme.Dim,
                TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
        }

        protected override void OnMouseEnter(EventArgs e) { base.OnMouseEnter(e); hover = true; Invalidate(); }
        protected override void OnMouseLeave(EventArgs e) { base.OnMouseLeave(e); hover = false; Invalidate(); }
    }

    // A number with − and + on either side of it, under a small label (held down, it keeps stepping).
    class Stepper : PaintedControl
    {
        readonly string label;
        readonly int min, max, step;
        readonly Func<int, string> format;
        readonly Timer repeat = new Timer();
        int value, hover, held;   // hover/held: -1 the minus side, 1 the plus side, 0 neither
        public event EventHandler ValueChanged;

        public Stepper(string label, int min, int max, int step, int value, Func<int, string> format)
        {
            this.label = label;
            this.min = min;
            this.max = max;
            this.step = step;
            this.value = value;
            this.format = format;
            Font = Theme.Body;
            Cursor = Cursors.Hand;
            repeat.Tick += delegate { repeat.Interval = 70; Bump(held); };
        }

        public int Value { get { return value; } }

        float LabelHeight { get { return Height * 0.36f; } }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            TextRenderer.DrawText(g, Lang.T(label), Theme.Small, new Rectangle(0, 0, Width, (int)LabelHeight), Theme.Dim,
                TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
            var r = new RectangleF(0.5f, LabelHeight, Width - 1.5f, Height - LabelHeight - 1.5f);
            using (GraphicsPath p = IconArt.RoundRect(r, r.Height / 2))
            {
                using (var b = new SolidBrush(Color.FromArgb(48, 48, 50))) g.FillPath(b, p);
                using (var pen = new Pen(Color.FromArgb(72, 72, 74))) g.DrawPath(pen, p);
            }
            float side = r.Height;
            for (int k = -1; k <= 1; k += 2)
            {
                var c = new RectangleF(k < 0 ? r.Left + 3 : r.Right - side + 3, r.Top + 3, side - 6, side - 6);
                bool can = k < 0 ? value > min : value < max;
                if (hover == k && can) using (var b = new SolidBrush(Color.FromArgb(66, 66, 68))) g.FillEllipse(b, c);
                TextRenderer.DrawText(g, k < 0 ? "" : "", Theme.Glyphs, Rectangle.Round(c), can ? Theme.Fg : Color.FromArgb(90, 90, 90),
                    TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
            }
            TextRenderer.DrawText(g, Lang.T(format(value)), Font, Rectangle.Round(new RectangleF(r.Left + side, r.Top, r.Width - 2 * side, r.Height)), Theme.Fg,
                TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine);
        }

        int SideAt(Point p)
        {
            if (p.Y < LabelHeight) return 0;
            float side = Height - LabelHeight;
            return p.X < side ? -1 : p.X > Width - side ? 1 : 0;
        }

        void Bump(int k)
        {
            int v = Math.Max(min, Math.Min(max, value + k * step));
            if (v == value) return;
            value = v;
            Invalidate();
            Raise(ValueChanged, this);
        }

        protected override void OnMouseMove(MouseEventArgs e)
        {
            base.OnMouseMove(e);
            int k = SideAt(e.Location);
            if (k != hover) { hover = k; Invalidate(); }
        }

        protected override void OnMouseLeave(EventArgs e) { base.OnMouseLeave(e); hover = 0; held = 0; repeat.Stop(); Invalidate(); }

        protected override void OnMouseDown(MouseEventArgs e)
        {
            base.OnMouseDown(e);
            held = SideAt(e.Location);
            if (held == 0) return;
            Bump(held);
            repeat.Interval = 400;
            repeat.Start();
        }

        protected override void OnMouseUp(MouseEventArgs e) { base.OnMouseUp(e); held = 0; repeat.Stop(); }

        protected override void Dispose(bool disposing)
        {
            if (disposing) repeat.Dispose();
            base.Dispose(disposing);
        }
    }

    // The focus timer's face: a ring that fills as the period runs (accent while working, green while resting), the
    // time in the middle, what is happening under it, and four dots for the periods done in this round of four.
    class FocusRing : PaintedControl
    {
        public Func<float> Progress = () => 0;
        public Func<string> TimeText = () => "", StateText = () => "";
        public Func<Color> Ink = () => Theme.Accent;
        public Func<int> Done = () => 0;
        readonly Font big = new Font(Lang.Family == "Segoe UI" ? "Segoe UI Light" : Lang.Family, 22f);

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            float thick = Height * 0.06f, d = Math.Min(Width, Height) - thick - 2;
            var r = new RectangleF((Width - d) / 2f, (Height - d) / 2f, d, d);
            using (var track = new Pen(Color.FromArgb(52, 52, 56), thick)) g.DrawEllipse(track, r);
            float p = Math.Max(0, Math.Min(1, Progress()));
            if (p > 0)
                using (var arc = new Pen(Ink(), thick) { StartCap = LineCap.Round, EndCap = LineCap.Round })
                    g.DrawArc(arc, r, -90, Math.Max(1f, 360 * p));
            int cy = Height / 2;
            TextRenderer.DrawText(g, TimeText(), big, new Rectangle(0, cy - (int)(Height * 0.24f), Width, (int)(Height * 0.3f)), Theme.Fg,
                TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine);
            TextRenderer.DrawText(g, StateText(), Theme.Small, new Rectangle(0, cy + (int)(Height * 0.07f), Width, (int)(Height * 0.13f)), Theme.Dim,
                TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine);
            float dot = Height * 0.035f, gap = dot * 2.6f, y = cy + Height * 0.25f;
            int done = Done();
            for (int i = 0; i < 4; i++)
            {
                float x = Width / 2f + (i - 1.5f) * gap;
                using (var b = new SolidBrush(i < done ? Ink() : Color.FromArgb(70, 70, 74))) g.FillEllipse(b, x - dot / 2, y - dot / 2, dot, dot);
            }
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing) big.Dispose();
            base.Dispose(disposing);
        }
    }
}
