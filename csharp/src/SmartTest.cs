using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Text;
using System.Threading;
using System.Windows.Forms;
using Timer = System.Windows.Forms.Timer;

namespace BlinkBuddy
{
    // Diagnostics: BlinkBuddy.exe --smart-test <output folder> <level 0..2>
    // Shows a test scene (a bright page and a dark page, each with text, colour chips and a photo-like gradient),
    // runs the smart filter over it, saves what was captured and what was shown (input.png, output.png) and the
    // chip colours before and after (report.txt), then lets it run against a changing screen and reports frame
    // rate, work per frame and delay.
    static class SmartTest
    {
        const int Chip = 44, Width = 960, Height = 400;
        static readonly string[] Names = { "orange", "sky blue", "pink", "yellow", "red", "dark blue", "grey", "near-bg" };

        [System.Runtime.InteropServices.DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();

        // BlinkBuddy.exe --refresh-test <report file>: how much of the screen Windows recomposes during one
        // LutGuard.ScreenRefresh, against the same time with the screen left alone, and whether focus stayed put.
        public static void RunRefresh(string report)
        {
            Native.SetProcessDPIAware();
            Rectangle screen = Screen.PrimaryScreen.Bounds;
            var gpu = new SmartFilter.Gpu();
            gpu.Init(IntPtr.Zero, screen);
            try
            {
                Func<int, long> changed = ms =>
                {
                    long total = 0, presented, px;
                    var clock = System.Diagnostics.Stopwatch.StartNew();
                    while (clock.ElapsedMilliseconds < ms)
                        if (gpu.NextFrame(20, out presented, out px) > 0) total += px;
                    return total;
                };
                changed(500);   // let go of whatever was pending
                long idle = changed(600);
                IntPtr fg = GetForegroundWindow();
                LutGuard.ScreenRefresh.Now();
                long refresh = changed(600);
                File.WriteAllText(report, string.Format("idle {0} refresh {1} screen {2} focus-kept {3}", idle, refresh,
                    (long)screen.Width * screen.Height, GetForegroundWindow() == fg));
            }
            finally { gpu.Dispose(); }
        }

        public static void Run(string folder, int level)
        {
            Native.SetProcessDPIAware();
            Application.EnableVisualStyles();
            Directory.CreateDirectory(folder);
            Rectangle wa = Screen.PrimaryScreen.WorkingArea, screen = Screen.PrimaryScreen.Bounds;
            var scene = new Form
            {
                FormBorderStyle = FormBorderStyle.None, StartPosition = FormStartPosition.Manual, TopMost = true, ShowInTaskbar = false,
                Bounds = new Rectangle(wa.Left + 40, wa.Top + 40, Width, Height + 6), BackColor = Color.Black,
                BackgroundImage = DrawScene(), BackgroundImageLayout = ImageLayout.None
            };
            // A strip that changes every timer tick, so new desktop frames keep coming like during video or scrolling.
            var ticker = new Panel { Bounds = new Rectangle(0, Height, Width, 6), BackColor = Color.Black };
            scene.Controls.Add(ticker);
            scene.Show();

            var smart = new SmartFilter();
            var points = new Point[Names.Length * 2];
            for (int side = 0; side < 2; side++)
                for (int i = 0; i < Names.Length; i++)
                    points[side * Names.Length + i] = new Point(scene.Left + side * Width / 2 + 24 + i * (Chip + 8) + Chip / 2 - screen.Left,
                                                                scene.Top + 150 + Chip / 2 - screen.Top);
            smart.TestPoints = points;
            smart.DumpRect = new Rectangle(scene.Left - screen.Left, scene.Top - screen.Top, Width, Height);
            smart.Apply(SmartFilter.ForLevel(level), SynchronizationContext.Current ?? new WindowsFormsSynchronizationContext());

            // Timeline (seconds): 0-3 settle and save images, 3-11 changing screen, 11-15 still screen,
            // 15-23 changing screen with the filter off (baseline for outside measurements), then exit.
            var report = new StringBuilder();
            report.AppendLine("Level: " + Comfort.Levels[level]);
            var clock = System.Diagnostics.Stopwatch.StartNew();
            int phase = 0, ticks = 0;
            var timer = new Timer { Interval = 15 };
            timer.Tick += delegate
            {
                ticker.BackColor = ++ticks % 2 == 0 ? Color.Black : Color.FromArgb(40, 40, 40);
                double t = clock.Elapsed.TotalSeconds;
                if (phase == 0 && t >= 2.5)
                {
                    smart.DumpDir = folder;
                    phase = 1;
                }
                else if (phase == 1 && t >= 3)
                {
                    Describe(smart, report);
                    smart.TestPoints = null;   // stop reading pixels back; it stalls the GPU and would skew timing
                    phase = 2;
                }
                else if (phase == 2 && t >= 11)
                {
                    report.AppendLine("Changing screen: " + smart.Status);
                    ticker.Visible = false;
                    phase = 3;
                }
                else if (phase == 3 && t >= 15)
                {
                    report.AppendLine("Still screen:    " + smart.Status);
                    smart.Stop();
                    ticker.Visible = true;
                    phase = 4;
                }
                else if (phase == 4 && t >= 23)
                {
                    timer.Stop();
                    File.WriteAllText(Path.Combine(folder, "report.txt"), report.ToString());
                    Application.ExitThread();
                }
            };
            timer.Start();
            Application.Run();
        }

        // Left: a bright page. Right: a dark page. Each has a heading, body text, faint text, colour chips (the last
        // one barely different from the page, to test that near-identical colours stay apart) and a photo-like image.
        // Diagnostics: BlinkBuddy.exe --layer-test <output folder> <level 0..2>
        // Same scene for Smart layer: saves input.png (no layer) and output.png (with the layer, made visible to capture
        // for that moment), the chip colours, then reports the layer's activity during small changes, big changes
        // (half the scene flipping bright/dark twice a second) and, for comparison, small changes with it off.
        public static void RunLayer(string folder, int level)
        {
            Native.SetProcessDPIAware();
            Application.EnableVisualStyles();
            Directory.CreateDirectory(folder);
            Rectangle wa = Screen.PrimaryScreen.WorkingArea;
            var scene = new Form
            {
                FormBorderStyle = FormBorderStyle.None, StartPosition = FormStartPosition.Manual, TopMost = true, ShowInTaskbar = false,
                Bounds = new Rectangle(wa.Left + 40, wa.Top + 40, Width, Height + 6), BackColor = Color.Black,
                BackgroundImage = DrawScene(), BackgroundImageLayout = ImageLayout.None
            };
            var ticker = new Panel { Bounds = new Rectangle(0, Height, Width, 6), BackColor = Color.Black };
            var flip = new Panel { Bounds = new Rectangle(0, 0, Width / 2, Height), BackColor = Color.FromArgb(30, 30, 30), Visible = false };
            scene.Controls.Add(ticker);
            scene.Controls.Add(flip);
            scene.Show();
            var area = new Rectangle(scene.Left, scene.Top, Width, Height);

            var layer = new SmartLayer();
            var report = new StringBuilder();
            report.AppendLine("Level: " + Comfort.Levels[level]);
            var clock = System.Diagnostics.Stopwatch.StartNew();
            int phase = 0, ticks = 0;
            var timer = new Timer { Interval = 15 };
            timer.Tick += delegate
            {
                ticker.BackColor = ++ticks % 2 == 0 ? Color.Black : Color.FromArgb(40, 40, 40);
                double t = clock.Elapsed.TotalSeconds;
                if (phase == 0 && t >= 0.5)
                {
                    Capture(area, Path.Combine(folder, "input.png")).Dispose();
                    layer.Apply(new SmartLayer.Params { Level = level, Gain = 1 }, SynchronizationContext.Current);
                    phase = 1;
                }
                else if (phase == 1 && t >= 3)
                {
                    layer.Frozen = true;
                    layer.ShowInCaptures(true);
                    phase = 2;
                }
                else if (phase == 2 && t >= 3.4)
                {
                    using (Bitmap shown = Capture(area, Path.Combine(folder, "output.png")))
                    {
                        report.AppendLine("Text lines (brightest pixel; darkest for the bright page heading), and the page around them:");
                        string[] lines = { "dark page heading", "dark page body text", "bright page heading (darkest pixel)" };
                        Rectangle[] spots = { new Rectangle(Width / 2 + 24, 22, 230, 36), new Rectangle(Width / 2 + 24, 72, 420, 20), new Rectangle(24, 22, 230, 36) };
                        for (int i = 0; i < lines.Length; i++)
                        {
                            int best = i < 2 ? 0 : 255;
                            for (int y = spots[i].Top; y < spots[i].Bottom; y++)
                                for (int x = spots[i].Left; x < spots[i].Right; x++)
                                {
                                    int v = shown.GetPixel(x, y).G;
                                    best = i < 2 ? Math.Max(best, v) : Math.Min(best, v);
                                }
                            report.AppendLine(string.Format("  {0,-38} {1}", lines[i], best));
                        }
                        report.AppendLine(string.Format("  {0,-38} {1}", "bright page background", shown.GetPixel(440, 300).G));
                        report.AppendLine(string.Format("  {0,-38} {1}", "dark page background next to its text", shown.GetPixel(Width / 2 + 300, 62).G));
                        report.AppendLine(string.Format("  {0,-38} {1}", "dark page background far from text", shown.GetPixel(Width - 30, 300).G));
                        report.AppendLine(string.Format("{0,-24} {1}", "chip", "shown"));
                        for (int side = 0; side < 2; side++)
                            for (int i = 0; i < Names.Length; i++)
                            {
                                Color c = shown.GetPixel(side * Width / 2 + 24 + i * (Chip + 8) + Chip / 2, 150 + Chip / 2);
                                report.AppendLine(string.Format("{0,-24} {1},{2},{3}", (side == 0 ? "bright page: " : "dark page:   ") + Names[i], c.R, c.G, c.B));
                            }
                    }
                    layer.ShowInCaptures(false);
                    layer.Frozen = false;
                    phase = 3;
                }
                else if (phase == 3 && t >= 11)
                {
                    report.AppendLine("Small changes: " + layer.Status);
                    phase = 4;
                }
                else if (phase == 4)
                {
                    if (ticks % 32 == 0) flip.Visible = !flip.Visible;   // about twice a second
                    if (t >= 19)
                    {
                        report.AppendLine("Big changes:   " + layer.Status);
                        flip.Visible = false;
                        layer.Stop();
                        phase = 5;
                    }
                }
                else if (phase == 5 && t >= 27)
                {
                    timer.Stop();
                    File.WriteAllText(Path.Combine(folder, "report.txt"), report.ToString());
                    Application.ExitThread();
                }
            };
            timer.Start();
            Application.Run();
        }

        static Bitmap Capture(Rectangle area, string path)
        {
            var bmp = new Bitmap(area.Width, area.Height);
            using (Graphics g = Graphics.FromImage(bmp)) g.CopyFromScreen(area.Location, Point.Empty, area.Size);
            bmp.Save(path, System.Drawing.Imaging.ImageFormat.Png);
            return bmp;
        }

        static Bitmap DrawScene()
        {
            var bmp = new Bitmap(Width, Height);
            using (Graphics g = Graphics.FromImage(bmp))
            {
                g.SmoothingMode = SmoothingMode.AntiAlias;
                g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.ClearTypeGridFit;
                for (int side = 0; side < 2; side++)
                {
                    bool bright = side == 0;
                    int x0 = side * Width / 2;
                    Color page = bright ? Color.White : Color.FromArgb(30, 30, 30), ink = bright ? Color.FromArgb(20, 20, 20) : Color.FromArgb(230, 230, 230);
                    using (var b = new SolidBrush(page)) g.FillRectangle(b, x0, 0, Width / 2, Height);
                    using (var big = new Font("Segoe UI Semibold", 20f))
                    using (var body = new Font("Segoe UI", 12f))
                    using (var ink1 = new SolidBrush(ink))
                    using (var ink2 = new SolidBrush(Color.FromArgb(128, 128, 128)))
                    {
                        g.DrawString(bright ? "A bright page" : "A dark page", big, ink1, x0 + 24, 20);
                        g.DrawString("Body text should stay crisp and easy to read.", body, ink1, x0 + 24, 70);
                        g.DrawString("Faint secondary text, links and captions.", body, ink2, x0 + 24, 100);
                    }
                    Color[] chips =
                    {
                        Color.FromArgb(255, 153, 0), Color.FromArgb(135, 206, 250), Color.FromArgb(255, 204, 204), Color.FromArgb(255, 235, 59),
                        Color.FromArgb(220, 30, 30), Color.FromArgb(20, 30, 140), Color.FromArgb(128, 128, 128),
                        bright ? Color.FromArgb(238, 238, 238) : Color.FromArgb(45, 45, 45)
                    };
                    for (int i = 0; i < chips.Length; i++)
                        using (var b = new SolidBrush(chips[i])) g.FillRectangle(b, x0 + 24 + i * (Chip + 8), 150, Chip, Chip);
                    // Photo-like gradient: hue across, light to dark down.
                    for (int y = 0; y < 150; y++)
                        for (int x = 0; x < 300; x++)
                            bmp.SetPixel(x0 + 24 + x, 225 + y, Hsv(x * 360.0 / 300, 0.8, 1 - y / 170.0));
                }
            }
            return bmp;
        }

        static Color Hsv(double h, double s, double v)
        {
            double c = v * s, x = c * (1 - Math.Abs(h / 60 % 2 - 1)), m = v - c, r = 0, g = 0, b = 0;
            if (h < 60) { r = c; g = x; } else if (h < 120) { r = x; g = c; } else if (h < 180) { g = c; b = x; }
            else if (h < 240) { g = x; b = c; } else if (h < 300) { r = x; b = c; } else { r = c; b = x; }
            return Color.FromArgb((int)((r + m) * 255), (int)((g + m) * 255), (int)((b + m) * 255));
        }

        static void Describe(SmartFilter smart, StringBuilder report)
        {
            if (smart.Failed || smart.TestIn == null)
            {
                report.AppendLine("No frames sampled. Status: " + smart.Status);
                return;
            }
            report.AppendLine(string.Format("{0,-24} {1,-14} {2}", "chip", "captured", "shown"));
            for (int i = 0; i < smart.TestIn.Length; i++)
                report.AppendLine(string.Format("{0,-24} {1,-14} {2}", (i < Names.Length ? "bright page: " : "dark page:   ") + Names[i % Names.Length],
                    Bgra(smart.TestIn[i]), Bgra(smart.TestOut[i])));
        }

        static string Bgra(int v) { return string.Format("{0},{1},{2}", (v >> 16) & 255, (v >> 8) & 255, v & 255); }
    }
}
