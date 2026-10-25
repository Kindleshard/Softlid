using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace BlinkBuddy
{
    static class Theme
    {
        public static readonly Color Bg = Color.FromArgb(32, 32, 32);
        public static readonly Color Card = Color.FromArgb(40, 40, 42);
        public static readonly Color Line = Color.FromArgb(58, 58, 58);
        public static readonly Color Fg = Color.FromArgb(242, 242, 242);
        public static readonly Color Dim = Color.FromArgb(160, 160, 160);
        public static readonly Color Track = Color.FromArgb(105, 105, 105);
        // The accent: BlinkBuddy's blue, another chosen colour, or Windows' own (see UseAccent).
        public static Color Accent = Color.FromArgb(76, 194, 255);
        // Old choices for the reading strip's tint and the accent (the colour picker offers them as quick picks).
        public static readonly Color[] StripColors =
        {
            Color.Black, Color.FromArgb(10, 18, 44), Color.FromArgb(46, 30, 14), Color.FromArgb(12, 34, 20),
            Color.FromArgb(38, 14, 40), Color.FromArgb(30, 36, 44)
        };
        public static readonly Color[] AccentColors =
        {
            Color.FromArgb(76, 194, 255), Color.FromArgb(64, 214, 196), Color.FromArgb(120, 210, 110), Color.FromArgb(240, 200, 70),
            Color.FromArgb(255, 163, 64), Color.FromArgb(255, 112, 112), Color.FromArgb(255, 128, 170), Color.FromArgb(176, 150, 255)
        };

        // Windows' accent, in the lighter shade Windows itself uses on dark backgrounds (null if it can't be read).
        public static Color? WindowsAccent()
        {
            try
            {
                using (var k = Microsoft.Win32.Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Explorer\Accent"))
                {
                    var palette = k == null ? null : k.GetValue("AccentPalette") as byte[];
                    if (palette != null && palette.Length >= 8) return Color.FromArgb(palette[4], palette[5], palette[6]);   // "light 2"
                    object menu = k == null ? null : k.GetValue("AccentColorMenu");
                    if (menu is int) { int abgr = (int)menu; return Color.FromArgb(abgr & 0xFF, (abgr >> 8) & 0xFF, (abgr >> 16) & 0xFF); }
                }
            }
            catch { }
            return null;
        }

        // Sets the accent from the settings: Windows' (when asked and readable) or the chosen one.
        public static void UseAccent(Settings s)
        {
            Color? windows = s.AccentFromWindows ? WindowsAccent() : null;
            Accent = windows ?? Rgb(s.AccentColor);
        }

        // A saved colour (ARGB), fully opaque.
        public static Color Rgb(int argb) { return Color.FromArgb(255, Color.FromArgb(argb)); }
        public static readonly Color Good = Color.FromArgb(108, 203, 95);     // all fine (up to date; the night mark)
        public static readonly Color Warn = Color.FromArgb(255, 163, 64);     // something to act on (an update)
        public static readonly Color Bad = Color.FromArgb(255, 105, 97);      // something failed
        // Old choices for the mini icon's glow (the first was the accent), read when moving old settings over.
        public static Color[] OrbitColors
        {
            get
            {
                return new[] { Accent, Color.FromArgb(64, 214, 196), Color.FromArgb(150, 226, 120), Color.FromArgb(255, 190, 90),
                    Color.FromArgb(255, 128, 170), Color.FromArgb(232, 232, 240) };
            }
        }
        public static readonly Font Body = new Font("Segoe UI", 10f);
        public static readonly Font Small = new Font("Segoe UI", 8.75f);
        public static readonly Font Heading = new Font("Segoe UI Semibold", 11.5f);
        // Windows 11's Segoe Fluent Icons where present (same code points), Windows 10's Segoe MDL2 Assets otherwise.
        public static readonly string GlyphFamily = new Font("Segoe Fluent Icons", 10f).Name == "Segoe Fluent Icons" ? "Segoe Fluent Icons" : "Segoe MDL2 Assets";
        public static readonly Font Glyphs = new Font(GlyphFamily, 10f);
        public static readonly Font SectionIcons = new Font(GlyphFamily, 14f);

        public static Label MakeLabel(string text, Font font, Color color, Rectangle bounds)
        {
            return new Label
            {
                Text = Lang.T(text), Font = font, ForeColor = color, BackColor = Color.Transparent, Bounds = bounds,
                TextAlign = ContentAlignment.MiddleLeft, UseMnemonic = false
            };
        }

        public static Button MakeButton(string text, Rectangle bounds)
        {
            var b = new SoftButton
            {
                Text = Lang.T(text), Bounds = bounds, FlatStyle = FlatStyle.Flat, Font = Body, Cursor = Cursors.Hand,
                BackColor = Color.FromArgb(55, 55, 55), ForeColor = Fg, UseVisualStyleBackColor = false
            };
            b.FlatAppearance.BorderColor = Color.FromArgb(75, 75, 75);
            b.FlatAppearance.MouseOverBackColor = Color.FromArgb(66, 66, 66);
            b.FlatAppearance.MouseDownBackColor = Color.FromArgb(46, 46, 46);
            return b;
        }
    }

    abstract class PaintedControl : Control
    {
        protected PaintedControl()
        {
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                     ControlStyles.ResizeRedraw | ControlStyles.SupportsTransparentBackColor, true);
            SetStyle(ControlStyles.StandardDoubleClick, false);
            BackColor = Theme.Bg;
        }

        protected static void Raise(EventHandler handler, object sender)
        {
            if (handler != null) handler(sender, EventArgs.Empty);
        }
    }

    class Slider : PaintedControl
    {
        int min, max;
        // A new range (e.g. the blue light filter allowing cooler than daylight); the value is kept within it.
        public void SetRange(int newMin, int newMax) { min = newMin; max = newMax; Value = Clamp(val); Invalidate(); }
        int val;
        bool dragging, hover;
        public event EventHandler ValueChanged;

        public Slider(int min, int max, int value)
        {
            this.min = min;
            this.max = max;
            val = Clamp(value);
            SetStyle(ControlStyles.Selectable, true);
            TabStop = true;
            Cursor = Cursors.Hand;
            Size = new Size(200, 30);
        }

        public int Value
        {
            get { return val; }
            set
            {
                int v = Clamp(value);
                if (v == val) return;
                val = v;
                Invalidate();
                Raise(ValueChanged, this);
            }
        }

        int Clamp(int v) { return Math.Max(min, Math.Min(max, v)); }
        float Radius { get { return Height * 0.3f; } }
        float Start { get { return Radius + 1; } }
        float End { get { return Width - Radius - 1; } }
        float ThumbX { get { return Start + (End - Start) * (val - min) / (max - min); } }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            float cy = Height / 2f, x = ThumbX, r = Radius, th = Math.Max(2f, Height * 0.13f);
            using (var pen = new Pen(Theme.Track, th)) { pen.StartCap = pen.EndCap = LineCap.Round; g.DrawLine(pen, x, cy, End, cy); }
            Color tint = muted ? Color.FromArgb(120, 120, 120) : Tint;
            if (Marker >= min && Marker <= max)
            {
                // A mark on the track (the night schedule's strength), drawn under the filled part's end and the thumb.
                float mx = Start + (End - Start) * (Marker - min) / (max - min), mr = Math.Max(3f, th * 1.25f);
                using (var b = new SolidBrush(muted ? Theme.Track : Theme.Good)) g.FillEllipse(b, mx - mr, cy - mr, 2 * mr, 2 * mr);
                using (var pen = new Pen(BackColor, Math.Max(1f, th * 0.4f))) g.DrawEllipse(pen, mx - mr, cy - mr, 2 * mr, 2 * mr);
            }
            if (x - Start > 1)
            {
                // Warm: the filled part runs from blue to the colour the screen is being warmed towards.
                using (Brush fill = muted ? new SolidBrush(tint) : Warm ? (Brush)new LinearGradientBrush(new PointF(Start - 1, 0), new PointF(x + 1, 0), Theme.Accent, tint)
                                         : new SolidBrush(Theme.Accent))
                using (var pen = new Pen(fill, th)) { pen.StartCap = pen.EndCap = LineCap.Round; g.DrawLine(pen, Start, cy, x, cy); }
            }
            using (var b = new SolidBrush(Color.FromArgb(69, 69, 69))) g.FillEllipse(b, x - r, cy - r, 2 * r, 2 * r);
            using (var pen = new Pen(Color.FromArgb(85, 85, 85))) g.DrawEllipse(pen, x - r, cy - r, 2 * r, 2 * r);
            float ir = r * (dragging ? 0.42f : hover || Focused ? 0.62f : 0.52f);
            using (var b = new SolidBrush(tint)) g.FillEllipse(b, x - ir, cy - ir, 2 * ir, 2 * ir);
        }

        // For the blue light filter: the colour turns from blue to amber as the value goes up.
        public bool Warm;
        // Greyed (e.g. the blue light filter while a colour effect is on), but still movable.
        bool muted;
        public bool Muted { get { return muted; } set { if (value != muted) { muted = value; Invalidate(); } } }
        // A value marked on the track in green (-1: none).
        int marker = -1;
        public int Marker { get { return marker; } set { if (value != marker) { marker = value; Invalidate(); } } }
        static readonly Color Amber = Color.FromArgb(255, 184, 64);
        Color Tint
        {
            get
            {
                if (!Warm) return Theme.Accent;
                // warmer than daylight toward amber; cooler (below 0, when allowed) toward an icy blue-white
                Color a = Theme.Accent, to = val >= 0 ? Amber : Color.FromArgb(200, 230, 255);
                float k = val >= 0 ? (float)val / Math.Max(1, max) : (float)val / Math.Min(-1, min);
                k = k * k * (3 - 2 * k);
                return Color.FromArgb((int)(a.R + (to.R - a.R) * k), (int)(a.G + (to.G - a.G) * k), (int)(a.B + (to.B - a.B) * k));
            }
        }

        void SetFromMouse(int mx) { Value = min + (int)Math.Round((mx - Start) / (End - Start) * (max - min)); }

        protected override void OnMouseDown(MouseEventArgs e)
        {
            base.OnMouseDown(e);
            if (e.Button != MouseButtons.Left) return;
            Focus();
            dragging = true;
            SetFromMouse(e.X);
            Invalidate();
        }

        protected override void OnMouseMove(MouseEventArgs e) { base.OnMouseMove(e); if (dragging) SetFromMouse(e.X); }
        protected override void OnMouseUp(MouseEventArgs e) { base.OnMouseUp(e); dragging = false; Invalidate(); }
        protected override void OnMouseEnter(EventArgs e) { base.OnMouseEnter(e); hover = true; Invalidate(); }
        protected override void OnMouseLeave(EventArgs e) { base.OnMouseLeave(e); hover = false; Invalidate(); }
        protected override void OnGotFocus(EventArgs e) { base.OnGotFocus(e); Invalidate(); }
        protected override void OnLostFocus(EventArgs e) { base.OnLostFocus(e); Invalidate(); }
        // Only once clicked into: otherwise the wheel scrolls the page (see ScrollPage).
        protected override void OnMouseWheel(MouseEventArgs e) { base.OnMouseWheel(e); if (Focused) Value += Math.Sign(e.Delta) * Math.Max(1, (max - min) / 40); }

        protected override bool IsInputKey(Keys k)
        {
            return k == Keys.Left || k == Keys.Right || k == Keys.Up || k == Keys.Down || base.IsInputKey(k);
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            base.OnKeyDown(e);
            if (e.KeyCode == Keys.Left || e.KeyCode == Keys.Down) Value--;
            else if (e.KeyCode == Keys.Right || e.KeyCode == Keys.Up) Value++;
        }
    }

    class Toggle : PaintedControl
    {
        bool on;
        public event EventHandler CheckedChanged;

        public Toggle(bool on)
        {
            this.on = on;
            SetStyle(ControlStyles.Selectable, true);
            TabStop = true;
            Cursor = Cursors.Hand;
            Size = new Size(44, 22);
        }

        public bool Checked
        {
            get { return on; }
            set
            {
                if (value == on) return;
                on = value;
                Invalidate();
                Raise(CheckedChanged, this);
            }
        }

        protected override void OnClick(EventArgs e) { base.OnClick(e); Checked = !Checked; }
        protected override void OnEnabledChanged(EventArgs e) { base.OnEnabledChanged(e); Invalidate(); }
        protected override void OnKeyDown(KeyEventArgs e) { base.OnKeyDown(e); if (e.KeyCode == Keys.Space) Checked = !Checked; }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            var r = new RectangleF(1, 1, Width - 3, Height - 3);
            // Greyed out when it can't be used (and then always shown as off).
            bool shownOn = on && Enabled;
            Color line = Enabled ? Theme.Dim : Theme.Line;
            using (GraphicsPath p = IconArt.RoundRect(r, r.Height / 2))
            {
                if (shownOn) { using (var b = new SolidBrush(Theme.Accent)) g.FillPath(b, p); }
                else { using (var pen = new Pen(line, Math.Max(1f, Height / 22f))) g.DrawPath(pen, p); }
            }
            float kr = r.Height / 2 - Height * 0.2f, cy = r.Y + r.Height / 2;
            float cx = shownOn ? r.Right - r.Height / 2 : r.X + r.Height / 2;
            using (var b = new SolidBrush(shownOn ? Color.Black : line)) g.FillEllipse(b, cx - kr, cy - kr, 2 * kr, 2 * kr);
        }
    }

    class TabStrip : PaintedControl
    {
        readonly string[] items;
        int selected;
        public event EventHandler SelectedChanged;

        public TabStrip(params string[] items)
        {
            this.items = Lang.T(items);
            Font = new Font("Segoe UI", 10.5f);
            Cursor = Cursors.Hand;
        }

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

        // An item that is a single Segoe MDL2 Assets character (U+E000 and up) is drawn as an icon, e.g. "" for a cog.
        Font glyphs;

        Font FontFor(string item)
        {
            if (item.Length != 1 || item[0] < '') return Font;
            if (glyphs == null || Math.Abs(glyphs.Size - Font.Size) > 0.01f) glyphs = new Font(Theme.GlyphFamily, Font.Size);
            return glyphs;
        }

        Rectangle[] ItemBounds()
        {
            var rects = new Rectangle[items.Length];
            int pad = Height / 4, x = 0;
            for (int i = 0; i < items.Length; i++)
            {
                int w = TextRenderer.MeasureText(items[i], FontFor(items[i])).Width + 2 * pad;
                rects[i] = new Rectangle(x, 0, w, Height);
                x += w;
            }
            return rects;
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            Rectangle[] rects = ItemBounds();
            for (int i = 0; i < items.Length; i++)
            {
                TextRenderer.DrawText(g, items[i], FontFor(items[i]), rects[i], i == selected ? Theme.Fg : Theme.Dim, BackColor,
                    TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
                if (i != selected) continue;
                float uw = Height * 0.4f, uh = Math.Max(2f, Height * 0.075f);
                var u = new RectangleF(rects[i].X + (rects[i].Width - uw) / 2, Height - uh - 1, uw, uh);
                using (GraphicsPath p = IconArt.RoundRect(u, uh / 2))
                using (var b = new SolidBrush(Theme.Accent))
                    g.FillPath(b, p);
            }
        }

        protected override void OnMouseDown(MouseEventArgs e)
        {
            base.OnMouseDown(e);
            Rectangle[] rects = ItemBounds();
            for (int i = 0; i < rects.Length; i++)
                if (rects[i].Contains(e.Location)) Selected = i;
        }
    }

    // A tiny monitor with five spots: the four corners and the centre.
    class CornerPicker : PaintedControl
    {
        int selected;
        public event EventHandler SelectedChanged;

        public CornerPicker(int selected)
        {
            this.selected = selected;
            Cursor = Cursors.Hand;
        }

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

        RectangleF Frame { get { return new RectangleF(1.5f, 1.5f, Width - 3, Height - 3); } }

        PointF Spot(int i)
        {
            RectangleF f = Frame;
            if (i == 4) return new PointF(f.X + f.Width / 2, f.Y + f.Height / 2);
            float m = f.Height * 0.24f;
            return new PointF(i % 2 == 0 ? f.Left + m : f.Right - m, i < 2 ? f.Top + m : f.Bottom - m);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            using (GraphicsPath p = IconArt.RoundRect(Frame, Height * 0.1f))
            {
                using (var b = new SolidBrush(Color.FromArgb(42, 42, 42))) g.FillPath(b, p);
                using (var pen = new Pen(Theme.Track, Math.Max(1f, Height / 60f))) g.DrawPath(pen, p);
            }
            for (int i = 0; i < 5; i++)
            {
                PointF c = Spot(i);
                float r = Height * (i == selected ? 0.11f : 0.065f);
                using (var b = new SolidBrush(i == selected ? Theme.Accent : Theme.Track))
                    g.FillEllipse(b, c.X - r, c.Y - r, 2 * r, 2 * r);
            }
        }

        protected override void OnMouseDown(MouseEventArgs e)
        {
            base.OnMouseDown(e);
            int best = 0;
            float bestD = float.MaxValue;
            for (int i = 0; i < 5; i++)
            {
                PointF c = Spot(i);
                float d = (c.X - e.X) * (c.X - e.X) + (c.Y - e.Y) * (c.Y - e.Y);
                if (d < bestD) { bestD = d; best = i; }
            }
            Selected = best;
        }
    }

    class EyeArt : PaintedControl
    {
        float lookX, lookY, open = 1f;

        public void Set(float x, float y, float openness)
        {
            lookX = x;
            lookY = y;
            open = openness;
            Invalidate();
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            // Leaves room below the eye for the lashes drawn when it closes.
            float pad = Width * 0.06f;
            var r = new RectangleF(pad, Height * 0.1f, Width - 2 * pad, Height * 0.62f);
            IconArt.DrawEye(e.Graphics, r, open, lookX, lookY, Math.Max(1.5f, Width * 0.035f));
        }
    }

    // Input brightness (left to right) against what is shown (bottom to top); the dashed diagonal is "unchanged".
    class CurveView : PaintedControl
    {
        public Func<double, double> Curve;

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            var r = new RectangleF(0.5f, 0.5f, Width - 1, Height - 1);
            using (var b = new SolidBrush(Color.FromArgb(40, 40, 40))) g.FillRectangle(b, r);
            using (var pen = new Pen(Theme.Line)) g.DrawRectangle(pen, r.X, r.Y, r.Width, r.Height);
            using (var pen = new Pen(Theme.Track) { DashStyle = DashStyle.Dash }) g.DrawLine(pen, r.Left, r.Bottom, r.Right, r.Top);
            if (Curve == null) return;
            var points = new PointF[65];
            for (int i = 0; i < points.Length; i++)
            {
                double x = i / 64.0;
                points[i] = new PointF(r.Left + (float)x * r.Width, r.Bottom - (float)Curve(x) * r.Height);
            }
            using (var pen = new Pen(Theme.Accent, Math.Max(1.5f, Height / 60f))) g.DrawLines(pen, points);
        }
    }

    // A row of mutually exclusive choices.
    class Segmented : PaintedControl
    {
        readonly string[] items;
        int selected;
        public event EventHandler SelectedChanged;

        public Segmented(params string[] items)
        {
            this.items = Lang.T(items);
            Font = Theme.Body;
            Cursor = Cursors.Hand;
        }

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
            var all = new RectangleF(0.5f, 0.5f, Width - 1, Height - 1);
            using (GraphicsPath p = IconArt.RoundRect(all, Height * 0.2f))
            {
                using (var b = new SolidBrush(Color.FromArgb(43, 43, 43))) g.FillPath(b, p);
                using (var pen = new Pen(Theme.Line)) g.DrawPath(pen, p);
            }
            float w = all.Width / items.Length;
            for (int i = 0; i < items.Length; i++)
            {
                var r = new RectangleF(all.X + i * w, all.Y, w, all.Height);
                if (i == selected)
                {
                    RectangleF inner = RectangleF.Inflate(r, -3, -3);
                    using (GraphicsPath p = IconArt.RoundRect(inner, Height * 0.15f))
                    using (var b = new SolidBrush(Color.FromArgb(70, 70, 70)))
                        g.FillPath(b, p);
                }
                TextRenderer.DrawText(g, items[i], Font, Rectangle.Round(r), i == selected ? Theme.Fg : Theme.Dim,
                    TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
            }
        }

        protected override void OnMouseDown(MouseEventArgs e)
        {
            base.OnMouseDown(e);
            Selected = Math.Max(0, Math.Min(items.Length - 1, e.X * items.Length / Math.Max(1, Width)));
        }
    }

    // A fully rounded button: a circle when it is square (an icon alone), a pill otherwise (icon and text).
    class RoundButton : PaintedControl
    {
        Image image;
        bool hover, down;

        public override string Text { get { return base.Text; } set { base.Text = Lang.T(value); } }

        public RoundButton()
        {
            Font = Theme.Body;
            Cursor = Cursors.Hand;
            SetStyle(ControlStyles.Selectable, true);
            TabStop = true;
        }

        public Image Image
        {
            get { return image; }
            set { image = value; Invalidate(); }
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            var r = new RectangleF(0.5f, 0.5f, Width - 1.5f, Height - 1.5f);
            Color fill = down ? Color.FromArgb(46, 46, 46) : hover ? Color.FromArgb(66, 66, 66) : Color.FromArgb(55, 55, 55);
            Color edge = Focused ? Theme.Accent : Color.FromArgb(75, 75, 75), ink = Theme.Fg;
            if (!Enabled) ink = Color.FromArgb(115, 115, 115);
            if (tint != Color.Empty)
            {
                // Tinted (e.g. orange for an update): a dark wash of the colour, its edge and its text.
                int k = down ? 40 : hover ? 70 : 52;
                fill = Color.FromArgb(fill.R + (tint.R - fill.R) * k / 255, fill.G + (tint.G - fill.G) * k / 255, fill.B + (tint.B - fill.B) * k / 255);
                edge = ink = tint;
            }
            using (GraphicsPath p = IconArt.RoundRect(r, r.Height / 2))
            {
                using (var b = new SolidBrush(fill)) g.FillPath(b, p);
                using (var pen = new Pen(edge)) g.DrawPath(pen, p);
            }
            if (image == null && Text.Length > 0 && Font.Name == Theme.GlyphFamily)
            {
                // an icon alone: centred by its own outline (font metrics leave it a pixel or so off), smoothed in grey
                using (var path = new GraphicsPath())
                {
                    path.AddString(Text, Font.FontFamily, (int)Font.Style, g.DpiY * Font.SizeInPoints / 72f, PointF.Empty, StringFormat.GenericTypographic);
                    RectangleF ink2 = path.GetBounds();
                    using (var m = new Matrix())
                    {
                        m.Translate(Width / 2f - (ink2.X + ink2.Width / 2f), Height / 2f - (ink2.Y + ink2.Height / 2f));
                        path.Transform(m);
                    }
                    using (var b = new SolidBrush(ink)) g.FillPath(b, path);
                }
                return;
            }
            if (image == null)
            {
                // text alone: centred in the whole button
                TextRenderer.DrawText(g, Text, Font, ClientRectangle, ink, TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine);
                return;
            }
            Size ts = Text.Length > 0 ? TextRenderer.MeasureText(Text, Font, Size.Empty, TextFormatFlags.NoPadding) : Size.Empty;   // measured as drawn
            int iw = image != null ? image.Width : 0, space = image != null && Text.Length > 0 ? Height / 4 : 0;
            int x = (Width - iw - space - ts.Width) / 2;
            if (image != null) g.DrawImage(image, x, (Height - image.Height) / 2, image.Width, image.Height);
            if (Text.Length > 0)
                TextRenderer.DrawText(g, Text, Font, new Point(x + iw + space, (Height - ts.Height) / 2), ink, TextFormatFlags.NoPadding);
        }

        Color tint = Color.Empty;
        public Color Tint { get { return tint; } set { tint = value; Invalidate(); } }

        protected override void OnMouseEnter(EventArgs e) { base.OnMouseEnter(e); hover = true; Invalidate(); }
        protected override void OnMouseLeave(EventArgs e) { base.OnMouseLeave(e); hover = down = false; Invalidate(); }
        protected override void OnMouseDown(MouseEventArgs e) { base.OnMouseDown(e); down = true; Invalidate(); }
        protected override void OnMouseUp(MouseEventArgs e) { base.OnMouseUp(e); down = false; Invalidate(); }
        protected override void OnGotFocus(EventArgs e) { base.OnGotFocus(e); Invalidate(); }
        protected override void OnLostFocus(EventArgs e) { base.OnLostFocus(e); Invalidate(); }
        protected override void OnTextChanged(EventArgs e) { base.OnTextChanged(e); Invalidate(); }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            base.OnKeyDown(e);
            if (e.KeyCode == Keys.Space || e.KeyCode == Keys.Enter) OnClick(EventArgs.Empty);
        }
    }

    // A few fixed steps on a line, o---o---o---o, with a label under each; the track is blue up to the chosen step.
    // Selected can be -1 (none of them, e.g. while a custom curve is in use).
    class StepSlider : PaintedControl
    {
        string[] labels;
        int selected = -1;
        bool dragging;
        public event EventHandler SelectedChanged;

        public StepSlider(params string[] labels)
        {
            this.labels = labels;
            Font = Theme.Small;
            Cursor = Cursors.Hand;
            SetStyle(ControlStyles.Selectable, true);
            TabStop = true;
        }

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

        // Fewer or more steps (compact and advanced views); the choice is kept if it still exists.
        public void SetSteps(params string[] steps)
        {
            labels = Lang.T(steps);
            if (selected >= steps.Length) selected = steps.Length - 1;
            Invalidate();
        }

        // Room at each end for half of the widest label, so the outer labels aren't cut off.
        float Pad
        {
            get
            {
                float widest = 0;
                foreach (string l in labels) widest = Math.Max(widest, TextRenderer.MeasureText(l, Font).Width);
                return Math.Max(Height * 0.3f, widest / 2f + 2);
            }
        }
        float X(int i) { return Pad + (Width - 2 * Pad) * i / Math.Max(1, labels.Length - 1); }
        float TrackY { get { return Height * 0.3f; } }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            float y = TrackY, th = Math.Max(2f, Height * 0.06f), r = Height * 0.12f;
            using (var pen = new Pen(Theme.Track, th)) g.DrawLine(pen, X(0), y, X(labels.Length - 1), y);
            if (selected > 0) using (var pen = new Pen(Theme.Accent, th)) g.DrawLine(pen, X(0), y, X(selected), y);
            for (int i = 0; i < labels.Length; i++)
            {
                float x = X(i);
                bool reached = selected >= 0 && i <= selected;
                using (var b = new SolidBrush(reached ? Theme.Accent : Theme.Track)) g.FillEllipse(b, x - r, y - r, 2 * r, 2 * r);
                if (i == selected)
                {
                    // the thumb, like the sliders'
                    float R = Height * 0.2f;
                    using (var b = new SolidBrush(Color.FromArgb(69, 69, 69))) g.FillEllipse(b, x - R, y - R, 2 * R, 2 * R);
                    using (var pen = new Pen(Color.FromArgb(85, 85, 85))) g.DrawEllipse(pen, x - R, y - R, 2 * R, 2 * R);
                    float ir = R * (dragging ? 0.42f : Focused ? 0.62f : 0.52f);
                    using (var b = new SolidBrush(Theme.Accent)) g.FillEllipse(b, x - ir, y - ir, 2 * ir, 2 * ir);
                }
                Size ts = TextRenderer.MeasureText(labels[i], Font);
                TextRenderer.DrawText(g, labels[i], Font, new Point((int)(x - ts.Width / 2f), (int)(Height * 0.56f)),
                    i == selected ? Theme.Fg : Theme.Dim, TextFormatFlags.NoPadding);
            }
        }

        int Nearest(int mx)
        {
            int best = 0;
            for (int i = 1; i < labels.Length; i++) if (Math.Abs(X(i) - mx) < Math.Abs(X(best) - mx)) best = i;
            return best;
        }

        protected override void OnMouseDown(MouseEventArgs e)
        {
            base.OnMouseDown(e);
            if (e.Button != MouseButtons.Left) return;
            Focus();
            dragging = true;
            Selected = Nearest(e.X);
            Invalidate();
        }

        protected override void OnMouseMove(MouseEventArgs e) { base.OnMouseMove(e); if (dragging) Selected = Nearest(e.X); }
        protected override void OnMouseUp(MouseEventArgs e) { base.OnMouseUp(e); dragging = false; Invalidate(); }
        protected override void OnGotFocus(EventArgs e) { base.OnGotFocus(e); Invalidate(); }
        protected override void OnLostFocus(EventArgs e) { base.OnLostFocus(e); Invalidate(); }
        protected override bool IsInputKey(Keys k) { return k == Keys.Left || k == Keys.Right || base.IsInputKey(k); }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            base.OnKeyDown(e);
            if (e.KeyCode == Keys.Left && selected > 0) Selected = selected - 1;
            else if (e.KeyCode == Keys.Right && selected < labels.Length - 1) Selected = selected + 1;
        }
    }

    // A page that can be taller than the window: Content scrolls under a thin bar on the right, by dragging the bar or
    // with the mouse wheel anywhere over the page (the settings window routes the wheel here).
    class ScrollPage : Panel
    {
        public readonly Panel Content = new Panel { BackColor = Theme.Bg, Location = Point.Empty };
        readonly Bar bar;
        int offset;

        public ScrollPage()
        {
            BackColor = Theme.Bg;
            bar = new Bar(this);
            Controls.Add(bar);
            Controls.Add(Content);
        }

        // Called after the rows are laid out, with Content's height already set.
        public void Refit()
        {
            Content.Width = ClientSize.Width;
            ScrollTo(offset);
        }

        public void ScrollBy(int delta) { ScrollTo(offset + delta); }
        public bool AtEnd { get { return offset >= Content.Height - ClientSize.Height; } }

        void ScrollTo(int y)
        {
            offset = Math.Max(0, Math.Min(y, Content.Height - ClientSize.Height));
            if (Content.Top != -offset)
            {
                Content.Top = -offset;
                Content.Invalidate(true);   // drawn afresh: copying the moved pixels along could leave bits of a row where it no longer is
            }
            int w = Math.Max(4, ClientSize.Width / 80);
            bar.Bounds = new Rectangle(ClientSize.Width - w - 2, 0, w, ClientSize.Height);
            bar.Visible = Content.Height > ClientSize.Height;
            bar.BringToFront();
            bar.Invalidate();
        }

        protected override void OnResize(EventArgs e)
        {
            base.OnResize(e);
            Refit();
        }

        class Bar : PaintedControl
        {
            readonly ScrollPage page;
            int grabY = -1, grabOffset;

            public Bar(ScrollPage page) { this.page = page; }

            RectangleF Thumb
            {
                get
                {
                    float total = Math.Max(1, page.Content.Height), view = page.ClientSize.Height;
                    float h = Math.Max(Width * 6, Height * view / total);
                    float y = (Height - h) * page.offset / Math.Max(1, total - view);
                    return new RectangleF(0, y, Width, h);
                }
            }

            protected override void OnPaint(PaintEventArgs e)
            {
                e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
                using (GraphicsPath p = IconArt.RoundRect(Thumb, Width / 2f))
                using (var b = new SolidBrush(grabY >= 0 ? Theme.Dim : Theme.Track))
                    e.Graphics.FillPath(b, p);
            }

            protected override void OnMouseDown(MouseEventArgs e)
            {
                base.OnMouseDown(e);
                grabY = e.Y;
                grabOffset = page.offset;
                if (!Thumb.Contains(e.Location))   // a click beside the thumb jumps there
                {
                    page.ScrollTo((int)((float)e.Y / Height * page.Content.Height - page.ClientSize.Height / 2f));
                    grabOffset = page.offset;
                }
                Invalidate();
            }

            protected override void OnMouseMove(MouseEventArgs e)
            {
                base.OnMouseMove(e);
                if (grabY < 0) return;
                float total = page.Content.Height, view = page.ClientSize.Height, track = Height - Thumb.Height;
                page.ScrollTo(grabOffset + (int)((e.Y - grabY) * (total - view) / Math.Max(1, track)));
            }

            protected override void OnMouseUp(MouseEventArgs e) { base.OnMouseUp(e); grabY = -1; Invalidate(); }
        }
    }

    // One option in a list of mutually exclusive choices: a radio dot, a title with an optional tag beside it, and a
    // one-line note underneath. The owner unchecks the others when Chosen fires.
    class ChoiceRow : PaintedControl
    {
        static readonly Color TagText = Color.FromArgb(240, 190, 100), TagFill = Color.FromArgb(66, 52, 30);
        readonly string title, note, tag;
        bool isChecked;
        Font small;
        public event EventHandler Chosen;

        public ChoiceRow(string title, string note, string tag)
        {
            this.title = Lang.T(title);
            this.note = Lang.T(note);
            this.tag = Lang.T(tag);
            Font = Theme.Body;
            Cursor = Cursors.Hand;
        }

        public bool Checked
        {
            get { return isChecked; }
            set
            {
                if (value == isChecked) return;
                isChecked = value;
                Invalidate();
            }
        }

        // Follows Font, which the window scales for the display.
        Font Small
        {
            get
            {
                float size = Font.Size * Theme.Small.Size / Theme.Body.Size;
                if (small == null || Math.Abs(small.Size - size) > 0.01f)
                {
                    if (small != null) small.Dispose();
                    small = new Font(Theme.Small.FontFamily, size);
                }
                return small;
            }
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            float u = Font.Height;
            if (isChecked)
            {
                using (GraphicsPath p = IconArt.RoundRect(new RectangleF(0.5f, 0.5f, Width - 1, Height - 1), u * 0.35f))
                {
                    using (var b = new SolidBrush(Color.FromArgb(43, 43, 43))) g.FillPath(b, p);
                    using (var pen = new Pen(Color.FromArgb(150, Theme.Accent))) g.DrawPath(pen, p);
                }
            }
            // Title and note centred as a block, with even room above and below inside the outline.
            float block = u * 1.1f + Small.Height, top = (Height - block) / 2;
            float cx = u * 0.95f, cy = top + u / 2, r = u * 0.36f;
            using (var pen = new Pen(isChecked ? Theme.Accent : Theme.Dim, Math.Max(1.5f, u / 12)))
                g.DrawEllipse(pen, cx - r, cy - r, 2 * r, 2 * r);
            if (isChecked)
                using (var b = new SolidBrush(Theme.Accent)) g.FillEllipse(b, cx - r * 0.5f, cy - r * 0.5f, r, r);

            var flags = TextFormatFlags.NoPadding | TextFormatFlags.SingleLine;
            int x = (int)(u * 1.8f), ty = (int)(cy - u / 2);
            TextRenderer.DrawText(g, title, Font, new Point(x, ty), Theme.Fg, flags);
            if (tag != null)
            {
                Size ts = TextRenderer.MeasureText(g, title, Font, Size.Empty, flags);
                Size gs = TextRenderer.MeasureText(g, tag, Small, Size.Empty, flags);
                var pill = new RectangleF(x + ts.Width + u * 0.5f, cy - gs.Height / 2f - u * 0.1f, gs.Width + u * 0.6f, gs.Height + u * 0.2f);
                using (GraphicsPath p = IconArt.RoundRect(pill, pill.Height * 0.3f))
                using (var b = new SolidBrush(TagFill))
                    g.FillPath(b, p);
                TextRenderer.DrawText(g, tag, Small, new Point((int)(pill.X + u * 0.3f), (int)(pill.Y + u * 0.1f)), TagText, flags);
            }
            var noteBox = new Rectangle(x, (int)(top + u * 1.1f), Width - x - (int)(u * 0.5f), (int)Small.Height + 2);
            TextRenderer.DrawText(g, note, Small, noteBox, Theme.Dim, TextFormatFlags.NoPadding | TextFormatFlags.WordBreak | TextFormatFlags.EndEllipsis);
        }

        protected override void OnMouseDown(MouseEventArgs e)
        {
            base.OnMouseDown(e);
            if (e.Button == MouseButtons.Left) Raise(Chosen, this);
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing && small != null) small.Dispose();
            base.Dispose(disposing);
        }
    }

    // Dark theme for the tray menu.
    class DarkMenuRenderer : ToolStripProfessionalRenderer
    {
        public DarkMenuRenderer() : base(new DarkMenuColors()) { RoundedEdges = false; }

        protected override void OnRenderItemText(ToolStripItemTextRenderEventArgs e)
        {
            e.TextColor = e.Item.Enabled ? Theme.Fg : Theme.Dim;
            base.OnRenderItemText(e);
        }

        protected override void OnRenderArrow(ToolStripArrowRenderEventArgs e)
        {
            e.ArrowColor = Theme.Fg;
            base.OnRenderArrow(e);
        }
    }

    class DarkMenuColors : ProfessionalColorTable
    {
        static readonly Color Back = Color.FromArgb(43, 43, 43), Hover = Color.FromArgb(64, 64, 64), Edge = Color.FromArgb(72, 72, 72);
        public override Color ToolStripDropDownBackground { get { return Back; } }
        public override Color ImageMarginGradientBegin { get { return Back; } }
        public override Color ImageMarginGradientMiddle { get { return Back; } }
        public override Color ImageMarginGradientEnd { get { return Back; } }
        public override Color MenuBorder { get { return Edge; } }
        public override Color MenuItemBorder { get { return Hover; } }
        public override Color MenuItemSelected { get { return Hover; } }
        public override Color MenuItemSelectedGradientBegin { get { return Hover; } }
        public override Color MenuItemSelectedGradientEnd { get { return Hover; } }
        public override Color MenuItemPressedGradientBegin { get { return Hover; } }
        public override Color MenuItemPressedGradientEnd { get { return Hover; } }
        public override Color SeparatorDark { get { return Edge; } }
        public override Color SeparatorLight { get { return Back; } }
        public override Color CheckBackground { get { return Theme.Accent; } }
        public override Color CheckSelectedBackground { get { return Theme.Accent; } }
        public override Color CheckPressedBackground { get { return Theme.Accent; } }
    }

    class ProgressLine : PaintedControl
    {
        float progress;

        public float Progress
        {
            get { return progress; }
            set { progress = Math.Max(0f, Math.Min(1f, value)); Invalidate(); }
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            var all = new RectangleF(0, 0, Width, Height);
            using (GraphicsPath p = IconArt.RoundRect(all, Height / 2f))
            using (var b = new SolidBrush(Theme.Line))
                g.FillPath(b, p);
            float w = Width * progress;
            if (w < Height) return;
            using (GraphicsPath p = IconArt.RoundRect(new RectangleF(0, 0, w, Height), Height / 2f))
            using (var b = new SolidBrush(Theme.Accent))
                g.FillPath(b, p);
        }
    }
}

namespace BlinkBuddy
{
    // Tooltips in the interface language.
    class LangTips : System.Windows.Forms.ToolTip
    {
        public new void SetToolTip(System.Windows.Forms.Control control, string caption) { base.SetToolTip(control, Lang.T(caption)); }
    }
}

namespace BlinkBuddy
{
    using System;
    using System.Drawing;
    using System.Drawing.Drawing2D;
    using System.Windows.Forms;

    // A button with fully rounded ends, drawn smoothly, in place of the square Windows one. It keeps Button's own
    // colours: BackColor, the FlatAppearance hover/press/border colours and ForeColor.
    class SoftButton : Button
    {
        bool hover, down;

        public SoftButton()
        {
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.Clear(Parent != null ? Parent.BackColor : Theme.Bg);
            g.SmoothingMode = SmoothingMode.AntiAlias;
            var r = new RectangleF(0.5f, 0.5f, Width - 1.5f, Height - 1.5f);
            Color fill = !Enabled ? BackColor : down ? FlatAppearance.MouseDownBackColor : hover ? FlatAppearance.MouseOverBackColor : BackColor;
            Color edge = Focused && ShowFocusCues ? Theme.Accent : FlatAppearance.BorderColor;
            using (GraphicsPath p = IconArt.RoundRect(r, r.Height / 2))
            {
                using (var b = new SolidBrush(fill)) g.FillPath(b, p);
                using (var pen = new Pen(edge)) g.DrawPath(pen, p);
            }
            TextRenderer.DrawText(g, Text, Font, ClientRectangle, Enabled ? ForeColor : Theme.Dim,
                TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis);
        }

        protected override void OnMouseEnter(EventArgs e) { base.OnMouseEnter(e); hover = true; Invalidate(); }
        protected override void OnMouseLeave(EventArgs e) { base.OnMouseLeave(e); hover = down = false; Invalidate(); }
        protected override void OnMouseDown(MouseEventArgs e) { base.OnMouseDown(e); down = true; Invalidate(); }
        protected override void OnMouseUp(MouseEventArgs e) { base.OnMouseUp(e); down = false; Invalidate(); }
    }

    // A row of colour dots to pick from; the chosen one has a ring.
    // The mini mode icons to choose from, each on a tile showing it as mini mode draws it (on its dark disc, with the
    // screen's brightness and warmth). The chosen tile has an accent edge; the one under the pointer plays its hover
    // animation. HoverChanged gives the index under the pointer (-1 when none), e.g. for a tooltip.
    class IconPicker : Control
    {
        readonly int count;
        readonly float[] awake;
        readonly Timer anim = new Timer { Interval = 15 };
        readonly Func<float> bright, warm;
        int selected, hover = -1;
        DateTime last;
        public event EventHandler SelectedChanged, HoverChanged;
        // Draws tile i instead of a mini icon (graphics, area, index, hover 0..1).
        public Action<Graphics, RectangleF, int, float> Painter;

        public IconPicker(int count, int selected, Func<float> bright, Func<float> warm)
        {
            this.count = count;
            this.selected = selected;
            this.bright = bright;
            this.warm = warm;
            awake = new float[count];
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            BackColor = Theme.Bg;
            Cursor = Cursors.Hand;
            for (int i = 0; i < count; i++) awake[i] = Target(i);
            anim.Tick += delegate { Step(); };
        }

        public int Selected
        {
            get { return selected; }
            set { if (value == selected) return; selected = value; Animate(); if (SelectedChanged != null) SelectedChanged(this, EventArgs.Empty); }
        }

        public int Hovered { get { return hover; } }

        float Tile { get { return Height; } }
        float Gap { get { return Height * 0.28f; } }
        float Target(int i) { return i == hover ? 1f : i == selected ? 0.75f : 0.45f; }

        int IndexAt(int x)
        {
            int i = (int)(x / (Tile + Gap));
            return i >= 0 && i < count && x - i * (Tile + Gap) <= Tile ? i : -1;
        }

        void Animate() { last = DateTime.Now; anim.Start(); Invalidate(); }

        void Step()
        {
            float dt = (float)Math.Min(0.05, (DateTime.Now - last).TotalSeconds);
            last = DateTime.Now;
            bool moving = false;
            for (int i = 0; i < count; i++)
            {
                float t = Target(i), step = 4f * dt;
                awake[i] = t > awake[i] ? Math.Min(t, awake[i] + step) : Math.Max(t, awake[i] - step);
                if (awake[i] != t) moving = true;
            }
            Invalidate();
            if (!moving) anim.Stop();
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            float s = Tile, b = bright(), w = warm();
            for (int i = 0; i < count; i++)
            {
                var r = new RectangleF(i * (s + Gap) + 1, 1, s - 2, s - 2);
                bool on = i == selected;
                using (GraphicsPath p = IconArt.RoundRect(r, s * 0.22f))
                {
                    using (var fill = new SolidBrush(on ? Color.FromArgb(52, 52, 56) : i == hover ? Color.FromArgb(46, 46, 50) : Color.FromArgb(40, 40, 43)))
                        g.FillPath(fill, p);
                    using (var pen = new Pen(on ? Theme.Accent : Color.FromArgb(68, 68, 72), on ? Math.Max(1.5f, s * 0.035f) : 1f))
                        g.DrawPath(pen, p);
                }
                float k = awake[i] * awake[i] * (3 - 2 * awake[i]);
                if (Painter != null)
                {
                    // something else on the tile (e.g. a colour effect's preview), inside its rounded edge
                    var inner = RectangleF.Inflate(r, -s * 0.12f, -s * 0.12f);
                    Painter(g, inner, i, k);
                    continue;
                }
                float d = s * 0.74f;
                var disc = new RectangleF(r.X + (r.Width - d) / 2, r.Y + (r.Height - d) / 2, d, d);
                using (var fill = new SolidBrush(Color.FromArgb(34, 36, 42))) g.FillEllipse(fill, disc);
                float box = d * 0.66f;
                IconArt.DrawMiniIcon(g, new RectangleF(disc.X + (d - box) / 2, disc.Y + (d - box) / 2, box, box), i, k, b, w);
            }
            if (!Enabled)   // greyed: e.g. colour effects while an engine that can't show them is in use
                using (var veil = new SolidBrush(Color.FromArgb(170, BackColor))) g.FillRectangle(veil, ClientRectangle);
        }

        protected override void OnEnabledChanged(EventArgs e) { base.OnEnabledChanged(e); Invalidate(); }

        protected override void OnMouseMove(MouseEventArgs e)
        {
            base.OnMouseMove(e);
            int i = IndexAt(e.X);
            if (i == hover) return;
            hover = i;
            Animate();
            if (HoverChanged != null) HoverChanged(this, EventArgs.Empty);
        }

        protected override void OnMouseLeave(EventArgs e)
        {
            base.OnMouseLeave(e);
            if (hover < 0) return;
            hover = -1;
            Animate();
            if (HoverChanged != null) HoverChanged(this, EventArgs.Empty);
        }

        protected override void OnMouseClick(MouseEventArgs e)
        {
            base.OnMouseClick(e);
            int i = IndexAt(e.X);
            if (i >= 0) Selected = i;
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing) anim.Dispose();
            base.Dispose(disposing);
        }
    }

    // The screen modes as a grid of compact tiles (four to a row), each an icon beside its name; the chosen one is outlined in
    // the accent. Selected can be -1 (none).
    class ModeGrid : Control
    {
        readonly string[] glyphs, names;
        int selected = -1, hover = -1;
        const int Columns = 4;
        public event EventHandler SelectedChanged;

        public ModeGrid(string[] glyphs, string[] names)
        {
            this.glyphs = glyphs;
            this.names = Lang.T(names);
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            BackColor = Theme.Bg;
            Cursor = Cursors.Hand;
        }

        public int Selected
        {
            get { return selected; }
            set { if (value == selected) return; selected = value; Invalidate(); }
        }

        int Rows { get { return (names.Length + Columns - 1) / Columns; } }
        float Gap { get { return Height * 0.06f; } }
        RectangleF Tile(int i)
        {
            float w = (Width - Gap * (Columns - 1)) / Columns, h = (Height - Gap * (Rows - 1)) / Rows;
            return new RectangleF((i % Columns) * (w + Gap), (i / Columns) * (h + Gap), w, h);
        }

        int IndexAt(Point p)
        {
            for (int i = 0; i < names.Length; i++) if (Tile(i).Contains(p)) return i;
            return -1;
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            using (var glyphFont = new Font(Theme.GlyphFamily, 11f))
                for (int i = 0; i < names.Length; i++)
                {
                    RectangleF r = Tile(i);
                    r.Inflate(-0.5f, -0.5f);
                    bool on = i == selected;
                    using (GraphicsPath p = IconArt.RoundRect(r, Math.Min(r.Height * 0.22f, 12f)))
                    {
                        using (var fill = new SolidBrush(on ? Color.FromArgb(50, 52, 58) : i == hover ? Color.FromArgb(46, 46, 50) : Color.FromArgb(40, 40, 43)))
                            g.FillPath(fill, p);
                        using (var pen = new Pen(on ? Theme.Accent : Color.FromArgb(64, 64, 68), on ? 1.6f : 1f)) g.DrawPath(pen, p);
                    }
                    // the icon and the name side by side, centred together (the name cut short if it can't fit)
                    const TextFormatFlags flags = TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine | TextFormatFlags.NoPadding;
                    int glyphW = TextRenderer.MeasureText(glyphs[i], glyphFont, Size.Empty, flags).Width, space = (int)(r.Height * 0.18f);
                    int pad = (int)(r.Height * 0.25f), room = (int)r.Width - 2 * pad - glyphW - space;
                    int nameW = Math.Min(room, TextRenderer.MeasureText(names[i], Theme.Small, Size.Empty, flags).Width);
                    int x = (int)(r.X + (r.Width - glyphW - space - nameW) / 2), y = (int)r.Y, h = (int)r.Height;
                    TextRenderer.DrawText(g, glyphs[i], glyphFont, new Rectangle(x, y, glyphW, h), on ? Theme.Accent : Theme.Fg, flags);
                    TextRenderer.DrawText(g, names[i], Theme.Small, new Rectangle(x + glyphW + space, y, nameW, h), on ? Theme.Fg : Theme.Dim,
                        flags | TextFormatFlags.EndEllipsis);
                }
        }

        protected override void OnMouseMove(MouseEventArgs e)
        {
            base.OnMouseMove(e);
            int i = IndexAt(e.Location);
            if (i != hover) { hover = i; Invalidate(); }
        }

        protected override void OnMouseLeave(EventArgs e)
        {
            base.OnMouseLeave(e);
            if (hover >= 0) { hover = -1; Invalidate(); }
        }

        protected override void OnMouseClick(MouseEventArgs e)
        {
            base.OnMouseClick(e);
            int i = IndexAt(e.Location);
            if (i < 0) return;
            selected = i;
            Invalidate();
            if (SelectedChanged != null) SelectedChanged(this, EventArgs.Empty);
        }
    }
}
