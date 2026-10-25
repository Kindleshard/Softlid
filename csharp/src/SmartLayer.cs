using System;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;

namespace BlinkBuddy
{
    // Smart layer: the smart filter's decision (dim each region by how bright its surroundings are, and keep anything
    // from ending up brighter than a calmed white page) without redrawing
    // the screen. A few times a second, and only if something changed, the graphics card measures a 1/16-resolution
    // brightness map of the desktop; the CPU turns it into a smooth darkening mask, and one click-through layer (the
    // mask, plus static grain baked in) sits over the screen. Apps keep drawing straight to the display, so nothing is
    // delayed, and the layer is only redrawn when the mask really changed. Black dims red, green and blue equally, so
    // colours keep their hue, and everything inside a region is dimmed together, so its detail keeps its contrast.
    class SmartLayer : IDisposable
    {
        public struct Params
        {
            public int Level;
            public float Gain;        // dimming the colour effect already applies, undone when measuring
            public bool Grain;
            public int GrainAlpha, GrainTone, GrainBlock;
        }

        const int LookEveryMs = 200;
        const int CellLevel = 2;     // brightness map cells are 2^(level+1) = 8 screen pixels

        volatile bool running;
        Thread thread;
        Layer layer;
        SynchronizationContext ui;
        readonly object gate = new object();
        Params pending;
        bool dirty;
        public volatile string Status = "";
        public bool Failed { get; private set; }
        public bool Running { get { return running; } }
        public Action Failure;
        public volatile bool Frozen;   // diagnostics: stop updating and let screen captures see the layer

        public void Apply(Params p, SynchronizationContext context)
        {
            lock (gate)
            {
                pending = p;
                dirty = true;
            }
            if (running || Failed) return;
            ui = context;
            Rectangle bounds = Screen.PrimaryScreen.Bounds;
            layer = new Layer(bounds);
            if (!layer.ExcludedFromCapture)
            {
                Fail("this Windows version can't hide the layer from capture");
                return;
            }
            Status = "Starting...";
            running = true;
            IntPtr hwnd = layer.Handle;
            thread = new Thread(() => Run(hwnd, bounds)) { IsBackground = true, Name = "BlinkBuddy smart layer" };
            thread.Start();
        }

        public void Stop()
        {
            running = false;
            if (thread != null)
            {
                thread.Join(1500);
                thread = null;
            }
            if (layer != null)
            {
                layer.Close();
                layer = null;
            }
        }

        public void ClearFailure() { Failed = false; }

        public void KeepOnTop()
        {
            if (running && layer != null && layer.Visible) Native.BringToTopNoActivate(layer.Handle);
        }

        // Diagnostics only: whether screen captures include the layer.
        public void ShowInCaptures(bool show)
        {
            if (layer != null) Native.SetWindowDisplayAffinity(layer.Handle, show ? 0u : 0x11u);
        }

        public void Dispose() { Stop(); }

        void Fail(string why)
        {
            Failed = true;
            running = false;
            Status = "Couldn't start (" + why + "). Using Auto comfort instead.";
            ui.Post(delegate
            {
                if (layer != null) { layer.Close(); layer = null; }
                if (Failure != null) Failure();
            }, null);
        }

        void Run(IntPtr hwnd, Rectangle bounds)
        {
            var gpu = new SmartFilter.Gpu();
            try { gpu.Init(IntPtr.Zero, bounds); }
            catch (Exception ex)
            {
                gpu.Dispose();
                Fail(ex.Message);
                return;
            }
            var surface = new Surface(hwnd, bounds);
            float[] map = null, peaks = null, shownMask = null;
            int mw = 0, mh = 0;
            int[] noise = null;
            string noiseKey = null;
            long screenPixels = (long)bounds.Width * bounds.Height, pendingChange = 0, lastLook = 0;
            long windowStart = Stopwatch.GetTimestamp(), workTicks = 0, drawnPixels = 0;
            int looks = 0, redraws = 0;
            bool shown = false;
            while (running)
            {
                if (Frozen) { Thread.Sleep(50); continue; }
                Params p;
                bool changed;
                lock (gate)
                {
                    p = pending;
                    changed = dirty;
                    dirty = false;
                }
                long presentedAt, changedPixels;
                int got = gpu.NextFrame(0, out presentedAt, out changedPixels);   // never waits: whatever changed since the last look
                if (got < 0)
                {
                    gpu.Reacquire();
                    Thread.Sleep(250);
                    continue;
                }
                long t0 = Stopwatch.GetTimestamp();
                if (got > 0) pendingChange += changedPixels;
                // Re-measure once at least 1% of the screen changed, or after 2 s if anything changed at all: a
                // blinking cursor or a ticking clock never costs a look.
                bool look = got > 0 && (map == null || pendingChange >= screenPixels / 100 ||
                                        (pendingChange > 0 && t0 - lastLook > Stopwatch.Frequency * 2));
                if (look)
                {
                    map = gpu.Probe(CellLevel, out mw, out mh);
                    peaks = gpu.ProbePeaks(mw, mh);
                    pendingChange = 0;
                    lastLook = t0;
                    looks++;
                }
                string nk = p.Grain ? p.GrainAlpha + "," + p.GrainTone + "," + p.GrainBlock : "";
                if (nk != noiseKey)
                {
                    noiseKey = nk;
                    noise = p.Grain ? Noise(p) : null;
                    changed = true;
                }
                if (map != null && (look || changed))
                {
                    float[] next = Mask(map, peaks, mw, mh, p);
                    Rectangle cells = changed || shownMask == null ? new Rectangle(0, 0, mw, mh) : ChangedCells(next, shownMask, mw, mh);
                    if (cells.Width > 0)
                    {
                        if (shownMask == null) shownMask = new float[next.Length];
                        for (int y = cells.Top; y < cells.Bottom; y++)
                            for (int x = cells.Left; x < cells.Right; x++) shownMask[y * mw + x] = next[y * mw + x];
                        Rectangle area = surface.Draw(shownMask, mw, mh, cells, noise, p.GrainBlock);
                        surface.Present(area);
                        drawnPixels += (long)area.Width * area.Height;
                        if (!shown)
                        {
                            shown = true;
                            ui.Post(delegate { if (layer != null) layer.Reveal(); }, null);
                        }
                        redraws++;
                    }
                }
                workTicks += Stopwatch.GetTimestamp() - t0;
                long now = Stopwatch.GetTimestamp();
                if (now - windowStart >= Stopwatch.Frequency * 5)
                {
                    double secs = (now - windowStart) / (double)Stopwatch.Frequency;
                    Status = string.Format("looked {0:0.#}x/s, redrew {1:0.#}x/s ({2:0}% of the screen each); {3:0.0} ms of work per second, no delay",
                        looks / secs, redraws / secs, redraws > 0 ? 100.0 * drawnPixels / redraws / screenPixels : 0,
                        workTicks * 1000.0 / Stopwatch.Frequency / secs);
                    looks = redraws = 0;
                    workTicks = drawnPixels = 0;
                    windowStart = now;
                }
                Thread.Sleep(LookEveryMs);
            }
            surface.Dispose();
            gpu.Dispose();
        }

        // Bounding box of the cells whose dimming moved by more than ~2.5/255, grown by one cell for the smoothing
        // between neighbours. Empty when nothing visible changed.
        static Rectangle ChangedCells(float[] next, float[] shown, int w, int h)
        {
            int x0 = w, y0 = h, x1 = -1, y1 = -1;
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++)
                    if (Math.Abs(next[y * w + x] - shown[y * w + x]) > 0.01f)
                    {
                        if (x < x0) x0 = x;
                        if (x > x1) x1 = x;
                        if (y < y0) y0 = y;
                        if (y > y1) y1 = y;
                    }
            if (x1 < 0) return Rectangle.Empty;
            x0 = Math.Max(0, x0 - 1); y0 = Math.Max(0, y0 - 1);
            x1 = Math.Min(w - 1, x1 + 1); y1 = Math.Min(h - 1, y1 + 1);
            return Rectangle.FromLTRB(x0, y0, x1 + 1, y1 + 1);
        }

        // Per-cell layer opacity: each cell's background is a brightness-aware blur of its neighbours (so bright and
        // dark regions don't bleed into each other), remapped exactly like Smart (GPU), and the cell is dimmed by
        // the same factor. Opacity is in the screen's encoded scale, which is where the layer blends.
        // Brightness-similarity weight exp(-d^2/2) for a difference of d stops, tabulated in 1/64-stop steps up to
        // 8 stops (beyond that it is effectively zero), so the 25-tap blur needs no exp() per tap.
        static readonly float[] Similar = BuildSimilar();

        static float[] BuildSimilar()
        {
            var t = new float[8 * 64 + 1];
            for (int i = 0; i < t.Length; i++) t[i] = (float)Math.Exp(-0.5 * (i / 64.0) * (i / 64.0));
            return t;
        }

        // Then the peak rule: no cell's brightest pixel may end up brighter than a calmed white page, which catches
        // thin white text and small bright icons that the average hides. Black lowers all channels together, so
        // this keeps hue; the cell's background darkens by the same factor, which on dark backgrounds is barely visible.
        static float[] Mask(float[] map, float[] peaks, int w, int h, Params p)
        {
            var l = new float[map.Length];
            double undo = Math.Pow(Math.Max(0.05, p.Gain), 2.2);
            double brightest = SmartFilter.Remap(p.Level, 0);   // where a white page ends up, in log2
            for (int i = 0; i < map.Length; i++) l[i] = (float)Math.Log(Math.Max(map[i] / undo, 1e-4), 2);
            var spatial = new float[25];
            for (int dy = -2; dy <= 2; dy++)
                for (int dx = -2; dx <= 2; dx++) spatial[(dy + 2) * 5 + dx + 2] = (float)Math.Exp(-(dx * dx + dy * dy) / 4.5);
            var a = new float[map.Length];
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++)
                {
                    float self = l[y * w + x], sum = 0, weights = 0;
                    for (int dy = -2; dy <= 2; dy++)
                    {
                        int row = Math.Min(h - 1, Math.Max(0, y + dy)) * w;
                        for (int dx = -2; dx <= 2; dx++)
                        {
                            float v = l[row + Math.Min(w - 1, Math.Max(0, x + dx))];
                            int d = (int)(Math.Abs(v - self) * 64);
                            float wgt = spatial[(dy + 2) * 5 + dx + 2] * (d < Similar.Length ? Similar[d] : 0) + 1e-3f;
                            sum += v * wgt;
                            weights += wgt;
                        }
                    }
                    double b = sum / weights;
                    double lk = SmartFilter.Remap(p.Level, b) - b;   // log2 of the linear-light factor, at most 0
                    double peak = Math.Log(Math.Max(peaks[y * w + x] / undo, 1e-4), 2);
                    lk = Math.Min(lk, brightest - peak);
                    a[y * w + x] = (float)(1 - Math.Pow(2, lk / 2.2));
                }
            return a;
        }

        // A 256x256 tile of grain specks (-255..255), the same distribution as the standalone grain.
        static int[] Noise(Params p)
        {
            var n = new int[256 * 256];
            uint state = 0x9E3779B9;
            for (int i = 0; i < n.Length; i++)
            {
                state ^= state << 13; state ^= state >> 17; state ^= state << 5;
                int v = (int)(state & 255) + (int)((state >> 8) & 255) - 255;
                n[i] = (Math.Abs(v) * p.GrainAlpha / 255) * (p.GrainTone == 2 || (p.GrainTone == 1 && v > 0) ? 1 : -1);
            }
            return n;
        }

        // The layer's pixels: a top-down 32-bit premultiplied bitmap that lives for as long as the layer, drawn into
        // directly and handed to the compositor with only the part that changed.
        class Surface : IDisposable
        {
            [StructLayout(LayoutKind.Sequential)]
            struct BitmapInfoHeader { public int Size, Width, Height; public short Planes, BitCount; public int Compression, SizeImage, XPpm, YPpm, ClrUsed, ClrImportant; }
            [StructLayout(LayoutKind.Sequential)]
            struct UpdateInfo { public int Size; public IntPtr DstDc, DstPoint, DstSize, SrcDc, SrcPoint; public int Key; public IntPtr Blend; public int Flags; public IntPtr Dirty; }

            [DllImport("gdi32.dll")] static extern IntPtr CreateDIBSection(IntPtr dc, ref BitmapInfoHeader bmi, uint usage, out IntPtr bits, IntPtr section, uint offset);
            [DllImport("gdi32.dll")] static extern IntPtr CreateCompatibleDC(IntPtr dc);
            [DllImport("gdi32.dll")] static extern IntPtr SelectObject(IntPtr dc, IntPtr obj);
            [DllImport("gdi32.dll")] static extern bool DeleteObject(IntPtr obj);
            [DllImport("gdi32.dll")] static extern bool DeleteDC(IntPtr dc);
            [DllImport("user32.dll")] static extern bool UpdateLayeredWindowIndirect(IntPtr hwnd, ref UpdateInfo info);

            readonly IntPtr hwnd, dc, dib, old, bits, block;   // block: unmanaged point, size, source point, blend, dirty rect
            readonly int w, h;
            int[] row;
            bool first = true;

            public Surface(IntPtr hwnd, Rectangle bounds)
            {
                this.hwnd = hwnd;
                w = bounds.Width;
                h = bounds.Height;
                var bmi = new BitmapInfoHeader { Size = 40, Width = w, Height = -h, Planes = 1, BitCount = 32 };
                dc = CreateCompatibleDC(IntPtr.Zero);
                dib = CreateDIBSection(dc, ref bmi, 0, out bits, IntPtr.Zero, 0);
                old = SelectObject(dc, dib);
                block = Marshal.AllocHGlobal(48);
                Marshal.WriteInt32(block, 0, bounds.X); Marshal.WriteInt32(block, 4, bounds.Y);   // destination point
                Marshal.WriteInt32(block, 8, w); Marshal.WriteInt32(block, 12, h);                // size
                Marshal.WriteInt64(block, 16, 0);                                                  // source point
                Marshal.WriteInt32(block, 24, 255 << 16 | 1 << 24);                                // AC_SRC_OVER, constant 255, AC_SRC_ALPHA
                row = new int[w];
            }

            // Upscales the cell mask smoothly (bilinear between cell centres) over the pixels those cells influence,
            // adding grain. Black at opacity a multiplies what's underneath by (1 - a); a light speck is white over it.
            public Rectangle Draw(float[] mask, int mw, int mh, Rectangle cells, int[] noise, int block)
            {
                float cellW = (float)w / mw, cellH = (float)h / mh;
                var area = Rectangle.Intersect(new Rectangle(0, 0, w, h), Rectangle.FromLTRB(
                    (int)(cells.Left * cellW), (int)(cells.Top * cellH), (int)Math.Ceiling(cells.Right * cellW), (int)Math.Ceiling(cells.Bottom * cellH)));
                if (first) area = new Rectangle(0, 0, w, h);
                block = Math.Max(1, block);
                var rowMask = new float[mw];
                for (int y = area.Top; y < area.Bottom; y++)
                {
                    float cy = Math.Max(0, Math.Min(mh - 1.001f, (y + 0.5f) / cellH - 0.5f));
                    int y0 = (int)cy;
                    float fy = cy - y0;
                    for (int i = 0; i < mw; i++) rowMask[i] = mask[y0 * mw + i] * (1 - fy) + mask[(y0 + 1) * mw + i] * fy;
                    int noiseRow = (y / block & 255) * 256;
                    for (int x = area.Left; x < area.Right; x++)
                    {
                        float cx = Math.Max(0, Math.Min(mw - 1.001f, (x + 0.5f) / cellW - 0.5f));
                        int x0 = (int)cx;
                        float fx = cx - x0;
                        int dim = (int)((rowMask[x0] * (1 - fx) + rowMask[x0 + 1] * fx) * 255 + 0.5f);
                        int px;
                        if (noise == null) px = dim << 24;
                        else
                        {
                            int g = noise[noiseRow + (x / block & 255)];
                            if (g <= 0) px = (dim + (-g) * (255 - dim) / 255) << 24;     // darker speck: more black
                            else px = ((g + dim * (255 - g) / 255) << 24) | (g << 16) | (g << 8) | g;   // white speck, premultiplied
                        }
                        row[x - area.Left] = px;
                    }
                    Marshal.Copy(row, 0, bits + (y * w + area.Left) * 4, area.Width);
                }
                return area;
            }

            public void Present(Rectangle area)
            {
                Marshal.WriteInt32(block, 28, area.Left); Marshal.WriteInt32(block, 32, area.Top);
                Marshal.WriteInt32(block, 36, area.Right); Marshal.WriteInt32(block, 40, area.Bottom);
                var info = new UpdateInfo
                {
                    Size = Marshal.SizeOf(typeof(UpdateInfo)), DstPoint = block, DstSize = block + 8, SrcDc = dc, SrcPoint = block + 16,
                    Blend = block + 24, Flags = 2,                                      // ULW_ALPHA
                    Dirty = first ? IntPtr.Zero : block + 28                            // only the changed part after the first time
                };
                UpdateLayeredWindowIndirect(hwnd, ref info);
                first = false;
            }

            public void Dispose()
            {
                SelectObject(dc, old);
                DeleteObject(dib);
                DeleteDC(dc);
                Marshal.FreeHGlobal(block);
            }
        }

        class Layer : Form
        {
            public readonly bool ExcludedFromCapture;

            public Layer(Rectangle bounds)
            {
                FormBorderStyle = FormBorderStyle.None;
                ShowInTaskbar = false;
                StartPosition = FormStartPosition.Manual;
                Bounds = bounds;
                IntPtr h = Handle;
                // Hidden from capture, so measuring the screen never sees the layer's own dimming.
                ExcludedFromCapture = Native.SetWindowDisplayAffinity(h, 0x11);   // WDA_EXCLUDEFROMCAPTURE
            }

            protected override bool ShowWithoutActivation { get { return true; } }

            protected override CreateParams CreateParams
            {
                get
                {
                    CreateParams cp = base.CreateParams;
                    cp.ExStyle |= Native.WS_EX_LAYERED | Native.WS_EX_TRANSPARENT | Native.WS_EX_TOOLWINDOW | Native.WS_EX_NOACTIVATE | Native.WS_EX_TOPMOST;
                    return cp;
                }
            }

            public void Reveal()
            {
                if (!Visible) Show();
                Native.BringToTopNoActivate(Handle);
            }
        }
    }
}
