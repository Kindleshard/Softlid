using System;
using System.Diagnostics;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Forms;

namespace BlinkBuddy
{
    // Smart filter: local tone mapping on the graphics card. It lowers the screen's overall contrast (big bright
    // areas, big dark areas) while keeping local contrast (text, edges, image detail, neighbouring colours) intact.
    //   Desktop Duplication hands over each new desktop image (only when something changed)
    //   -> pass 1 writes log-brightness at half resolution; its mipmaps are blurred "backgrounds" at every scale
    //   -> pass 2 gives each pixel an edge-aware background estimate, remaps that background (bright toward
    //      mid-grey, dark a little deeper, never brighter) and scales the pixel by the same factor, so its contrast
    //      with its surroundings is unchanged; one factor for R, G and B keeps hue and saturation
    //   -> the result goes to a click-through, always-on-top window excluded from capture, so it never sees itself.
    // Only processed frames are ever shown, so bright content can't flash; the price is about one refresh of delay.
    class SmartFilter : IDisposable
    {
        // Compress: how much bright backgrounds move toward mid-grey (1 = not at all). Expand: how much dark ones
        // move away from it (1 = not at all). DimStops: extra overall dimming. All in log-brightness (stops).
        public struct Params { public float R, G, B, Compress, Expand, DimStops; public bool Invert; }

        // Per strength (Comfort.Levels). A white page ends up at about 80, 68, 58 and 40% (as stored), in line with Standard.
        static readonly float[] Compress = { 0.8f, 0.65f, 0.5f, 0.35f }, Expand = { 1.05f, 1.15f, 1.25f, 1.3f }, Dim = { -0.2f, -0.35f, -0.5f, -1.3f };
        const double Anchor = -2.47;   // log2 of mid-grey (18%)

        public static Params ForLevel(int level)
        {
            return new Params { R = 1, G = 1, B = 1, Compress = Compress[level], Expand = Expand[level], DimStops = Dim[level] };
        }

        // New log2 brightness for a background of log2 brightness b: bright toward mid-grey, dark a little deeper,
        // then dimmed; never brighter. Shared with Smart layer so both modes agree.
        public static double Remap(int level, double b)
        {
            double nb = (b > Anchor ? Anchor + (b - Anchor) * Compress[level] : Anchor + (b - Anchor) * Expand[level]) + Dim[level];
            return Math.Min(nb, b);
        }

        // What a large flat area of this brightness (0..1, as stored) becomes, for the settings graph.
        public static double Flat(int level, double x)
        {
            if (x <= 0) return 0;
            double b = Math.Log(Math.Pow(x, 2.2), 2);
            return Math.Pow(Math.Pow(2, Remap(level, b)), 1 / 2.2);
        }

        const string Hlsl = @"
Texture2D src : register(t0);
Texture2D<float> lum : register(t1);
SamplerState bilinear : register(s0);
// mul: rgb = brightness and warmth, w = invert. tone: x = compress, y = expand, z = dim (stops), w = background mip level.
// size: xy = 1 / output size, zw = 1 / size of the half-resolution brightness map.
cbuffer Params : register(b0) { float4 mul; float4 tone; float4 size; };

static const float3 Luma = float3(0.2126, 0.7152, 0.0722);
static const float Anchor = -2.47;

float3 fetch(int2 p)
{
    float3 c = src.Load(int3(p, 0)).rgb;
    return mul.w > 0.5 ? 1 - c : c;
}

float lin(float3 c) { return dot(pow(c, 2.2), Luma); }

float4 VS(uint id : SV_VertexID) : SV_Position
{
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

// Pass 1, half resolution: linear brightness of each 2x2 block. Averaging in linear light (not log) means a few
// black glyphs lower a cell only by their share of its area, so text doesn't drag the page's background down.
float PSLum(float4 pos : SV_Position) : SV_Target
{
    int2 p = int2(pos.xy) * 2;
    return 0.25 * (lin(fetch(p)) + lin(fetch(p + int2(1, 0))) + lin(fetch(p + int2(0, 1))) + lin(fetch(p + int2(1, 1))));
}

// Smart layer, one output pixel per 8x8 cell: linear brightness of the cell's brightest pixel (thin white text,
// small bright icons), which the cell's average hides.
float PSPeak(float4 pos : SV_Position) : SV_Target
{
    int2 p = int2(pos.xy) * 8;
    float m = 0;
    [unroll] for (int j = 0; j < 8; j++)
        [unroll] for (int i = 0; i < 8; i++)
            m = max(m, lin(fetch(p + int2(i, j))));
    return m;
}

// The pixel's background: nine samples of the blurred map, each trusted by how close it is to the pixel's own
// brightness (sigma one stop), so a white page doesn't bleed into the dark text or photo next to it (no halos).
// The pixel itself counts too: a small element that clearly stands out (a chip, a glyph) then follows its own
// brightness and is lit evenly instead of mottled, while large areas and near-identical neighbours share one
// background, so subtle differences between them are kept.
float background(float2 uv, float l)
{
    float2 step = size.zw * exp2(tone.w);
    float sum = 1.5 * l, weights = 1.5;
    [unroll] for (int j = -1; j <= 1; j++)
        [unroll] for (int i = -1; i <= 1; i++)
        {
            float b = log2(max(lum.SampleLevel(bilinear, uv + float2(i, j) * step, tone.w), 1e-4));
            // Gaussian falloff across the nine samples hides the coarse map's cell boundaries.
            float w = exp(-0.5 * (b - l) * (b - l) - 0.5 * (i * i + j * j)) + 0.001;
            sum += b * w;
            weights += w;
        }
    return sum / weights;
}

float4 PS(float4 pos : SV_Position) : SV_Target
{
    float3 c = fetch(int2(pos.xy));
    float l = log2(max(lin(c), 1e-4));
    float b = background(pos.xy * size.xy, l);
    // Remap the background: bright ones toward mid-grey, dark ones a little deeper, then dim; never brighter.
    float nb = (b > Anchor ? Anchor + (b - Anchor) * tone.x : Anchor + (b - Anchor) * tone.y) + tone.z;
    // The pixel moves exactly as much as its background, so its contrast with its surroundings is unchanged,
    // and one factor for all three channels keeps hue and saturation (no clipping: the factor is never above 1).
    float k = exp2(min(nb - b, 0));
    return float4(saturate(c * pow(k, 1 / 2.2) * mul.rgb), 1);
}";

        volatile bool running;
        Thread thread;
        Overlay overlay;
        SynchronizationContext ui;
        readonly object gate = new object();
        Params pending;
        bool dirty;
        long changedAt;
        public volatile string Status = "";
        public bool Failed { get; private set; }
        public bool Running { get { return running; } }
        public Action Failure;   // called on the UI thread when the filter gives up

        // Diagnostics (--smart-test): output-relative points sampled on each frame as BGRA, before and after the shader.
        public Point[] TestPoints;
        public int[] TestIn, TestOut;
        public volatile int TestFrames;
        public volatile string DumpDir;   // set to save input.png and output.png of DumpRect from the next frame
        public Rectangle DumpRect;

        // Starts the filter if needed, otherwise just updates its settings. UI thread only.
        public void Apply(Params p, SynchronizationContext context)
        {
            lock (gate)
            {
                pending = p;
                dirty = true;
                changedAt = Stopwatch.GetTimestamp();
            }
            if (running || Failed) return;
            ui = context;
            Rectangle bounds = Screen.PrimaryScreen.Bounds;
            overlay = new Overlay(bounds);
            if (!overlay.ExcludedFromCapture)
            {
                Fail("this Windows version can't hide the filter window from capture");
                return;
            }
            overlay.Show();   // fully transparent until the first filtered frame is ready
            IntPtr hwnd = overlay.Handle;
            Status = "Starting...";
            running = true;
            thread = new Thread(() => Run(hwnd, bounds)) { IsBackground = true, Name = "BlinkBuddy smart filter" };
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
            if (overlay != null)
            {
                overlay.Close();
                overlay = null;
            }
        }

        public void ClearFailure() { Failed = false; }

        // Other always-on-top windows (the taskbar, for one) can rise above it; called once a second.
        public void KeepOnTop()
        {
            if (running && overlay != null) Native.BringToTopNoActivate(overlay.Handle);
        }

        public void Dispose() { Stop(); }

        void Fail(string why)
        {
            Failed = true;
            running = false;
            Status = "Couldn't start (" + why + "). Using the standard curve instead.";
            Post(delegate
            {
                if (overlay != null) { overlay.Close(); overlay = null; }
                if (Failure != null) Failure();
            });
        }

        void Post(Action a) { ui.Post(delegate { a(); }, null); }

        void Run(IntPtr hwnd, Rectangle bounds)
        {
            var gpu = new Gpu();
            try { gpu.Init(hwnd, bounds); }
            catch (Exception ex)
            {
                gpu.Dispose();
                Fail(ex.Message);
                return;
            }
            bool shown = false, haveFrame = false;
            double refreshMs = 1000.0 / Math.Max(24, gpu.RefreshHz), tick = Stopwatch.Frequency / 1000.0;
            long frames = 0, procTicks = 0, lagTicks = 0, windowStart = Stopwatch.GetTimestamp();
            while (running)
            {
                Params p;
                bool changed;
                long changedTicks;
                lock (gate)
                {
                    p = pending;
                    changed = dirty;
                    dirty = false;
                    changedTicks = changedAt;
                }
                if (changed) gpu.SetParams(p);
                // Wait long while idle; react fast for two seconds after a setting changes.
                bool adjusting = Stopwatch.GetTimestamp() - changedTicks < Stopwatch.Frequency * 2;
                long presentedAt;
                int got = gpu.NextFrame(adjusting ? 15u : 500u, out presentedAt);
                if (got < 0)
                {
                    // Lost the desktop (lock screen, UAC prompt, resolution change, exclusive full-screen game).
                    // Never leave a frozen picture up: go transparent until frames flow again.
                    if (shown) { shown = false; Post(delegate { if (overlay != null) overlay.Conceal(); }); }
                    haveFrame = false;
                    Thread.Sleep(250);
                    gpu.Reacquire();
                    continue;
                }
                if (got > 0) haveFrame = true;
                if (!haveFrame || (got == 0 && !changed)) continue;

                long t0 = Stopwatch.GetTimestamp();
                gpu.Render(this);
                long t1 = Stopwatch.GetTimestamp();
                if (!shown) { shown = true; Post(delegate { if (overlay != null) overlay.Reveal(); }); }
                if (got > 0)
                {
                    frames++;
                    procTicks += t1 - t0;
                    if (presentedAt > 0) lagTicks += t1 - presentedAt;
                }
                if (t1 - windowStart >= Stopwatch.Frequency)
                {
                    double secs = (t1 - windowStart) / (double)Stopwatch.Frequency;
                    Status = frames > 0
                        ? string.Format("{0:0} fps, {1:0.0} ms of work per frame, about {2:0} ms behind the screen",
                            frames / secs, procTicks / tick / frames, lagTicks / tick / frames + refreshMs)
                        : "Idle: nothing on screen is changing, so nothing runs.";
                    frames = procTicks = lagTicks = 0;
                    windowStart = t1;
                }
            }
            gpu.Dispose();
        }

        class Overlay : Form
        {
            public readonly bool ExcludedFromCapture;

            public Overlay(Rectangle bounds)
            {
                FormBorderStyle = FormBorderStyle.None;
                ShowInTaskbar = false;
                StartPosition = FormStartPosition.Manual;
                Bounds = bounds;
                BackColor = Color.Black;
                IntPtr h = Handle;
                Native.SetLayeredWindowAttributes(h, 0, 0, 2);   // LWA_ALPHA, invisible for now
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

            protected override void OnPaintBackground(PaintEventArgs e) { }   // Direct3D owns these pixels

            public void Reveal()
            {
                Native.SetLayeredWindowAttributes(Handle, 0, 255, 2);
                Native.BringToTopNoActivate(Handle);
            }

            public void Conceal() { Native.SetLayeredWindowAttributes(Handle, 0, 0, 2); }
        }

        // Everything Direct3D, owned by the thread that created it. COM methods are called through their vtable slots
        // (numbers taken from the Windows SDK headers). Without a window it only measures the screen (Smart layer).
        internal class Gpu : IDisposable
        {
            static readonly Guid IID_IDXGIFactory1 = new Guid("770aae78-f26f-4dba-a829-253c83d1b387");
            static readonly Guid IID_IDXGIOutput1 = new Guid("00cddea8-939b-4b83-a340-a685226666cc");
            static readonly Guid IID_ID3D11Texture2D = new Guid("6f15aaf2-d208-4e89-9ab4-489535d34f9c");
            const int DXGI_ERROR_WAIT_TIMEOUT = unchecked((int)0x887A0027);

            [StructLayout(LayoutKind.Sequential)]
            struct TexDesc { public uint Width, Height, MipLevels, ArraySize; public int Format; public uint SampleCount, SampleQuality; public int Usage; public uint BindFlags, CpuAccess, Misc; }
            [StructLayout(LayoutKind.Sequential)]
            struct BufferDesc { public uint ByteWidth; public int Usage; public uint BindFlags, CpuAccess, Misc, Stride; }
            [StructLayout(LayoutKind.Sequential)]
            struct Viewport { public float X, Y, Width, Height, MinDepth, MaxDepth; }
            [StructLayout(LayoutKind.Sequential)]
            struct SamplerDesc
            {
                public int Filter, AddressU, AddressV, AddressW;
                public float MipLodBias;
                public uint MaxAnisotropy;
                public int Comparison;
                public float Border0, Border1, Border2, Border3, MinLod, MaxLod;
            }
            [StructLayout(LayoutKind.Sequential)]
            struct Mapped { public IntPtr Data; public uint RowPitch, DepthPitch; }
            [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
            struct OutputDesc
            {
                [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string DeviceName;
                public int Left, Top, Right, Bottom, Attached, Rotation;
                public IntPtr Monitor;
            }
            [StructLayout(LayoutKind.Sequential)]
            struct FrameInfo
            {
                public long LastPresentTime, LastMouseUpdateTime;
                public uint AccumulatedFrames;
                public int RectsCoalesced, ProtectedContentMaskedOut, PointerX, PointerY, PointerVisible;
                public uint TotalMetadataBufferSize, PointerShapeBufferSize;
            }
            [StructLayout(LayoutKind.Sequential)]
            struct SwapDesc
            {
                public uint Width, Height, RefreshNum, RefreshDen;
                public int Format, ScanlineOrdering, Scaling;
                public uint SampleCount, SampleQuality, BufferUsage, BufferCount;
                public IntPtr OutputWindow;
                public int Windowed, SwapEffect;
                public uint Flags;
            }

            [DllImport("dxgi.dll")] static extern int CreateDXGIFactory1(ref Guid riid, out IntPtr factory);
            [DllImport("d3d11.dll")] static extern int D3D11CreateDevice(IntPtr adapter, int driverType, IntPtr software, uint flags, IntPtr levels, uint numLevels, uint sdk, out IntPtr device, out int level, out IntPtr context);
            [DllImport("d3dcompiler_47.dll", CharSet = CharSet.Ansi)] static extern int D3DCompile(byte[] src, IntPtr size, string name, IntPtr defines, IntPtr include, string entry, string target, uint flags1, uint flags2, out IntPtr code, out IntPtr errors);
            [DllImport("gdi32.dll", CharSet = CharSet.Unicode)] static extern IntPtr CreateDC(string driver, string device, string output, IntPtr init);
            [DllImport("gdi32.dll")] static extern int GetDeviceCaps(IntPtr dc, int index);
            [DllImport("gdi32.dll")] static extern bool DeleteDC(IntPtr dc);

            delegate int EnumFn(IntPtr self, uint index, out IntPtr item);
            delegate int OutputDescFn(IntPtr self, out OutputDesc desc);
            delegate int DuplicateFn(IntPtr self, IntPtr device, out IntPtr dup);
            delegate int AcquireFn(IntPtr self, uint timeoutMs, out FrameInfo info, out IntPtr resource);
            delegate int ReleaseFrameFn(IntPtr self);
            delegate int CreateSwapChainFn(IntPtr self, IntPtr device, ref SwapDesc desc, out IntPtr swap);
            delegate int PresentFn(IntPtr self, uint sync, uint flags);
            delegate int GetBufferFn(IntPtr self, uint index, ref Guid riid, out IntPtr surface);
            delegate int CreateBufferFn(IntPtr self, ref BufferDesc desc, IntPtr init, out IntPtr buffer);
            delegate int CreateTexture2DFn(IntPtr self, ref TexDesc desc, IntPtr init, out IntPtr texture);
            delegate int CreateViewFn(IntPtr self, IntPtr resource, IntPtr desc, out IntPtr view);
            delegate int CreateShaderFn(IntPtr self, IntPtr code, IntPtr size, IntPtr linkage, out IntPtr shader);
            delegate void TexGetDescFn(IntPtr self, out TexDesc desc);
            delegate IntPtr BlobPtrFn(IntPtr self);
            delegate void SetViewsFn(IntPtr self, uint start, uint count, IntPtr[] items);
            delegate void SetShaderFn(IntPtr self, IntPtr shader, IntPtr instances, uint count);
            delegate void DrawFn(IntPtr self, uint count, uint start);
            delegate int MapFn(IntPtr self, IntPtr resource, uint sub, int type, uint flags, out Mapped mapped);
            delegate void UnmapFn(IntPtr self, IntPtr resource, uint sub);
            delegate void TopologyFn(IntPtr self, int topology);
            delegate void SetTargetsFn(IntPtr self, uint count, IntPtr[] views, IntPtr depth);
            delegate void ViewportsFn(IntPtr self, uint count, ref Viewport vp);
            delegate void CopyFn(IntPtr self, IntPtr dst, IntPtr src);
            delegate void UpdateFn(IntPtr self, IntPtr resource, uint sub, IntPtr box, float[] data, uint rowPitch, uint depthPitch);
            delegate int CreateSamplerFn(IntPtr self, ref SamplerDesc desc, out IntPtr sampler);
            delegate void GenerateMipsFn(IntPtr self, IntPtr view);
            delegate void CopyRegionFn(IntPtr self, IntPtr dst, uint dstSub, uint x, uint y, uint z, IntPtr src, uint srcSub, IntPtr box);
            delegate int FrameRectsFn(IntPtr self, uint bufferSize, IntPtr buffer, out uint required);

            static T Fn<T>(IntPtr obj, int slot) where T : class
            {
                IntPtr vtable = Marshal.ReadIntPtr(obj);
                return (T)(object)Marshal.GetDelegateForFunctionPointer(Marshal.ReadIntPtr(vtable, slot * IntPtr.Size), typeof(T));
            }

            static void Check(int hr, string what)
            {
                if (hr < 0) throw new Exception(string.Format("{0} failed, 0x{1:X8}", what, hr));
            }

            IntPtr factory, adapter, output, output1, device, ctx, dup, swap, backBuffer, rtv, vs, ps, psLum, cb, sampler,
                   source, srv, lumTex, lumRtv, lumSrv, stageIn, stageOut, stageMap, psPeak, peakTex, peakRtv, stagePeak;
            uint width, height, lumWidth, lumHeight;
            public int RefreshHz = 60;
            readonly IntPtr[] one = new IntPtr[1], two = new IntPtr[2];
            readonly float[] constants = new float[12];

            // Cached context and duplication calls, used every frame.
            AcquireFn acquire;
            ReleaseFrameFn releaseFrame;
            PresentFn present;
            SetTargetsFn setTargets;
            ViewportsFn setViewports;
            TopologyFn setTopology;
            SetShaderFn setVS, setPS;
            SetViewsFn setResources, setConstants, setSamplers;
            DrawFn draw;
            CopyFn copy;
            UpdateFn update;
            GenerateMipsFn generateMips;

            public void Init(IntPtr hwnd, Rectangle bounds)
            {
                Guid iid = IID_IDXGIFactory1;
                Check(CreateDXGIFactory1(ref iid, out factory), "Creating the DXGI factory");
                string deviceName = FindOutput(bounds);
                IntPtr dc = CreateDC(deviceName, null, null, IntPtr.Zero);
                if (dc != IntPtr.Zero) { RefreshHz = GetDeviceCaps(dc, 116); DeleteDC(dc); }   // VREFRESH

                int level;
                Check(D3D11CreateDevice(adapter, 0, IntPtr.Zero, 0x20, IntPtr.Zero, 0, 7, out device, out level, out ctx), "Creating the Direct3D 11 device");
                iid = IID_IDXGIOutput1;
                Check(Marshal.QueryInterface(output, ref iid, out output1), "Getting IDXGIOutput1");
                Check(Fn<DuplicateFn>(output1, 22)(output1, device, out dup), "Starting Desktop Duplication");

                width = (uint)bounds.Width;
                height = (uint)bounds.Height;
                if (hwnd != IntPtr.Zero)
                {
                    // Flip model is the efficient one; fall back to the older blit model if the window type refuses it.
                    var desc = new SwapDesc
                    {
                        Width = width, Height = height, Format = 87, SampleCount = 1, BufferUsage = 0x20,   // B8G8R8A8_UNORM, render target output
                        BufferCount = 2, OutputWindow = hwnd, Windowed = 1, SwapEffect = 4                 // FLIP_DISCARD
                    };
                    var createSwap = Fn<CreateSwapChainFn>(factory, 10);
                    if (createSwap(factory, device, ref desc, out swap) < 0)
                    {
                        desc.BufferCount = 1;
                        desc.SwapEffect = 0;   // DISCARD
                        Check(createSwap(factory, device, ref desc, out swap), "Creating the swap chain");
                    }
                    iid = IID_ID3D11Texture2D;
                    Check(Fn<GetBufferFn>(swap, 9)(swap, 0, ref iid, out backBuffer), "Getting the back buffer");
                    Check(Fn<CreateViewFn>(device, 9)(device, backBuffer, IntPtr.Zero, out rtv), "Creating the render target");
                    present = Fn<PresentFn>(swap, 8);
                }

                vs = CompileShader("VS", "vs_4_0", 12);
                ps = CompileShader("PS", "ps_4_0", 15);
                psLum = CompileShader("PSLum", "ps_4_0", 15);
                var bd = new BufferDesc { ByteWidth = 48, BindFlags = 4 };   // constant buffer
                Check(Fn<CreateBufferFn>(device, 3)(device, ref bd, IntPtr.Zero, out cb), "Creating the constant buffer");

                // Half-resolution log-brightness map with a full mip chain: each level is a wider blur of the screen.
                lumWidth = (width + 1) / 2;
                lumHeight = (height + 1) / 2;
                var ld = new TexDesc
                {
                    Width = lumWidth, Height = lumHeight, MipLevels = 0, ArraySize = 1, Format = 54, SampleCount = 1,   // R16_FLOAT, all mips
                    BindFlags = 0x20 | 0x8, Misc = 0x1                                                                   // render target + shader resource, generate mips
                };
                Check(Fn<CreateTexture2DFn>(device, 5)(device, ref ld, IntPtr.Zero, out lumTex), "Creating the brightness map");
                Check(Fn<CreateViewFn>(device, 9)(device, lumTex, IntPtr.Zero, out lumRtv), "Creating the brightness map target");
                Check(Fn<CreateViewFn>(device, 7)(device, lumTex, IntPtr.Zero, out lumSrv), "Creating the brightness map view");
                var sd = new SamplerDesc { Filter = 0x15, AddressU = 3, AddressV = 3, AddressW = 3, MaxAnisotropy = 1, Comparison = 1, MaxLod = float.MaxValue };   // trilinear, clamp
                Check(Fn<CreateSamplerFn>(device, 23)(device, ref sd, out sampler), "Creating the sampler");

                // Backgrounds are judged about 1/50 of the screen height across, whatever the resolution.
                constants[7] = (float)Math.Max(0, Math.Round(Math.Log(lumHeight / 50.0, 2)));
                constants[8] = 1f / width;
                constants[9] = 1f / height;
                constants[10] = 1f / lumWidth;
                constants[11] = 1f / lumHeight;

                acquire = Fn<AcquireFn>(dup, 8);
                releaseFrame = Fn<ReleaseFrameFn>(dup, 14);
                setResources = Fn<SetViewsFn>(ctx, 8);
                setPS = Fn<SetShaderFn>(ctx, 9);
                setVS = Fn<SetShaderFn>(ctx, 11);
                draw = Fn<DrawFn>(ctx, 13);
                setConstants = Fn<SetViewsFn>(ctx, 16);
                setTopology = Fn<TopologyFn>(ctx, 24);
                setTargets = Fn<SetTargetsFn>(ctx, 33);
                setViewports = Fn<ViewportsFn>(ctx, 44);
                copy = Fn<CopyFn>(ctx, 47);
                update = Fn<UpdateFn>(ctx, 48);
                setSamplers = Fn<SetViewsFn>(ctx, 10);
                generateMips = Fn<GenerateMipsFn>(ctx, 54);
                SetParams(new Params { R = 1, G = 1, B = 1, Compress = 1, Expand = 1 });   // defined contents before the first draw
            }

            // Smart layer: linear brightness of the latest desktop image, averaged over cells of 2^(level+1) pixels.
            public float[] Probe(int level, out int w, out int h)
            {
                w = (int)Math.Max(1, lumWidth >> level);
                h = (int)Math.Max(1, lumHeight >> level);
                setTopology(ctx, 4);
                setVS(ctx, vs, IntPtr.Zero, 0);
                one[0] = cb;
                setConstants(ctx, 0, 1, one);
                two[0] = srv;
                two[1] = IntPtr.Zero;
                setResources(ctx, 0, 2, two);
                one[0] = lumRtv;
                setTargets(ctx, 1, one, IntPtr.Zero);
                var vp = new Viewport { Width = lumWidth, Height = lumHeight, MaxDepth = 1 };
                setViewports(ctx, 1, ref vp);
                setPS(ctx, psLum, IntPtr.Zero, 0);
                draw(ctx, 3, 0);
                one[0] = IntPtr.Zero;
                setTargets(ctx, 1, one, IntPtr.Zero);   // release it as a target so its mipmaps can be built
                generateMips(ctx, lumSrv);
                if (stageMap == IntPtr.Zero)
                {
                    var d = new TexDesc { Width = (uint)w, Height = (uint)h, MipLevels = 1, ArraySize = 1, Format = 54, SampleCount = 1, Usage = 3, CpuAccess = 0x20000 };
                    Check(Fn<CreateTexture2DFn>(device, 5)(device, ref d, IntPtr.Zero, out stageMap), "Creating the probe texture");
                }
                Fn<CopyRegionFn>(ctx, 46)(ctx, stageMap, 0, 0, 0, 0, lumTex, (uint)level, IntPtr.Zero);
                return ReadMap(stageMap, w, h);
            }

            // Smart layer: brightest pixel of each 8x8 cell, on the same w x h grid as Probe(2, ...).
            public float[] ProbePeaks(int w, int h)
            {
                if (psPeak == IntPtr.Zero)
                {
                    psPeak = CompileShader("PSPeak", "ps_4_0", 15);
                    var d = new TexDesc { Width = (uint)w, Height = (uint)h, MipLevels = 1, ArraySize = 1, Format = 54, SampleCount = 1, BindFlags = 0x20 };
                    Check(Fn<CreateTexture2DFn>(device, 5)(device, ref d, IntPtr.Zero, out peakTex), "Creating the peak map");
                    Check(Fn<CreateViewFn>(device, 9)(device, peakTex, IntPtr.Zero, out peakRtv), "Creating the peak map target");
                    d.BindFlags = 0; d.Usage = 3; d.CpuAccess = 0x20000;
                    Check(Fn<CreateTexture2DFn>(device, 5)(device, ref d, IntPtr.Zero, out stagePeak), "Creating the peak probe texture");
                }
                two[0] = srv;
                two[1] = IntPtr.Zero;
                setResources(ctx, 0, 2, two);
                one[0] = peakRtv;
                setTargets(ctx, 1, one, IntPtr.Zero);
                var vp = new Viewport { Width = w, Height = h, MaxDepth = 1 };
                setViewports(ctx, 1, ref vp);
                setPS(ctx, psPeak, IntPtr.Zero, 0);
                draw(ctx, 3, 0);
                one[0] = IntPtr.Zero;
                setTargets(ctx, 1, one, IntPtr.Zero);
                copy(ctx, stagePeak, peakTex);
                return ReadMap(stagePeak, w, h);
            }

            float[] ReadMap(IntPtr staging, int w, int h)
            {
                var map = new float[w * h];
                Mapped m;
                if (Fn<MapFn>(ctx, 14)(ctx, staging, 0, 1, 0, out m) < 0) return map;
                for (int y = 0; y < h; y++)
                    for (int x = 0; x < w; x++)
                        map[y * w + x] = Half((ushort)Marshal.ReadInt16(m.Data, y * (int)m.RowPitch + x * 2));
                Fn<UnmapFn>(ctx, 15)(ctx, staging, 0);
                return map;
            }

            static float Half(ushort h)
            {
                int e = (h >> 10) & 31, mant = h & 1023;
                float v = e == 0 ? mant * 5.9604645e-8f : e == 31 ? float.MaxValue : (float)((1024 + mant) * Math.Pow(2, e - 25));
                return (h & 0x8000) != 0 ? -v : v;
            }

            // The adapter and output that show this screen (on hybrid-GPU laptops that isn't always the default GPU).
            string FindOutput(Rectangle bounds)
            {
                var enumAdapters = Fn<EnumFn>(factory, 7);
                for (uint a = 0; ; a++)
                {
                    IntPtr ad;
                    if (enumAdapters(factory, a, out ad) < 0) break;
                    var enumOutputs = Fn<EnumFn>(ad, 7);
                    for (uint o = 0; ; o++)
                    {
                        IntPtr outp;
                        if (enumOutputs(ad, o, out outp) < 0) break;
                        OutputDesc d;
                        Fn<OutputDescFn>(outp, 7)(outp, out d);
                        if (d.Left == bounds.Left && d.Top == bounds.Top && d.Right == bounds.Right && d.Bottom == bounds.Bottom)
                        {
                            adapter = ad;
                            output = outp;
                            return d.DeviceName;
                        }
                        Marshal.Release(outp);
                    }
                    Marshal.Release(ad);
                }
                throw new Exception("couldn't find the display on any graphics adapter");
            }

            IntPtr CompileShader(string entry, string target, int createSlot)
            {
                byte[] src = Encoding.ASCII.GetBytes(Hlsl);
                IntPtr code, errors;
                int hr = D3DCompile(src, new IntPtr(src.Length), "BlinkBuddy", IntPtr.Zero, IntPtr.Zero, entry, target, 1 << 15, 0, out code, out errors);
                if (hr < 0)
                {
                    string msg = errors != IntPtr.Zero ? Marshal.PtrToStringAnsi(Fn<BlobPtrFn>(errors, 3)(errors)) : "";
                    throw new Exception("Compiling the shader failed: " + msg);
                }
                if (errors != IntPtr.Zero) Marshal.Release(errors);
                IntPtr shader;
                try
                {
                    Check(Fn<CreateShaderFn>(device, createSlot)(device, Fn<BlobPtrFn>(code, 3)(code), Fn<BlobPtrFn>(code, 4)(code), IntPtr.Zero, out shader), "Creating the " + entry + " shader");
                }
                finally { Marshal.Release(code); }
                return shader;
            }

            public void SetParams(Params p)
            {
                constants[0] = p.R; constants[1] = p.G; constants[2] = p.B; constants[3] = p.Invert ? 1 : 0;
                constants[4] = p.Compress; constants[5] = p.Expand; constants[6] = p.DimStops;   // [7..11] are fixed at Init
                update(ctx, cb, 0, IntPtr.Zero, constants, 0, 0);
            }

            // 1 = a new desktop image was copied, 0 = nothing new (timeout or only the pointer moved), -1 = desktop lost.
            public int NextFrame(uint timeoutMs, out long presentedAt)
            {
                long changedPixels;
                return NextFrame(timeoutMs, out presentedAt, out changedPixels);
            }

            IntPtr metadata;
            uint metadataSize;

            // Same, also reporting how many pixels changed (from Windows' dirty and move rectangles).
            public int NextFrame(uint timeoutMs, out long presentedAt, out long changedPixels)
            {
                presentedAt = 0;
                changedPixels = 0;
                if (dup == IntPtr.Zero) return -1;
                FrameInfo info;
                IntPtr resource;
                int hr = acquire(dup, timeoutMs, out info, out resource);
                if (hr == DXGI_ERROR_WAIT_TIMEOUT) return 0;
                if (hr < 0) return -1;
                int result = 0;
                try
                {
                    if (info.LastPresentTime != 0)
                    {
                        changedPixels = ChangedPixels(info.TotalMetadataBufferSize);
                        Guid iid = IID_ID3D11Texture2D;
                        IntPtr tex;
                        if (Marshal.QueryInterface(resource, ref iid, out tex) >= 0)
                        {
                            if (source == IntPtr.Zero) CreateSource(tex);
                            copy(ctx, source, tex);
                            Marshal.Release(tex);
                            presentedAt = info.LastPresentTime;
                            result = 1;
                        }
                    }
                }
                finally
                {
                    Marshal.Release(resource);
                    releaseFrame(dup);   // hand the desktop image back as early as possible
                }
                return result;
            }

            // Area of the rectangles Windows says changed in this frame (moved areas count by their destination).
            long ChangedPixels(uint needed)
            {
                if (needed == 0) return 0;
                if (needed > metadataSize)
                {
                    if (metadata != IntPtr.Zero) Marshal.FreeHGlobal(metadata);
                    metadata = Marshal.AllocHGlobal((int)needed);
                    metadataSize = needed;
                }
                long area = 0;
                uint used;
                if (Fn<FrameRectsFn>(dup, 10)(dup, metadataSize, metadata, out used) >= 0)   // move rects: POINT source + RECT destination
                    for (int o = 0; o + 24 <= used; o += 24)
                        area += RectArea(metadata, o + 8);
                if (Fn<FrameRectsFn>(dup, 9)(dup, metadataSize, metadata, out used) >= 0)    // dirty rects
                    for (int o = 0; o + 16 <= used; o += 16)
                        area += RectArea(metadata, o);
                return area;
            }

            static long RectArea(IntPtr p, int offset)
            {
                long w = Marshal.ReadInt32(p, offset + 8) - Marshal.ReadInt32(p, offset);
                long h = Marshal.ReadInt32(p, offset + 12) - Marshal.ReadInt32(p, offset + 4);
                return Math.Max(0, w) * Math.Max(0, h);
            }

            void CreateSource(IntPtr desktop)
            {
                TexDesc d;
                Fn<TexGetDescFn>(desktop, 10)(desktop, out d);
                d.MipLevels = 1; d.ArraySize = 1; d.Usage = 0; d.BindFlags = 8; d.CpuAccess = 0; d.Misc = 0;   // shader resource
                Check(Fn<CreateTexture2DFn>(device, 5)(device, ref d, IntPtr.Zero, out source), "Creating the source texture");
                Check(Fn<CreateViewFn>(device, 7)(device, source, IntPtr.Zero, out srv), "Creating the source view");
            }

            public void Reacquire()
            {
                Release(ref dup);
                Release(ref srv);
                Release(ref source);
                Release(ref stageIn);
                IntPtr d;
                if (Fn<DuplicateFn>(output1, 22)(output1, device, out d) >= 0) dup = d;
            }

            public void Render(SmartFilter owner)
            {
                setTopology(ctx, 4);   // triangle list; each pass is one triangle covering its target
                setVS(ctx, vs, IntPtr.Zero, 0);
                one[0] = cb;
                setConstants(ctx, 0, 1, one);

                // Pass 1: the half-resolution brightness map (unbind it as an input before drawing into it).
                two[0] = srv;
                two[1] = IntPtr.Zero;
                setResources(ctx, 0, 2, two);
                one[0] = lumRtv;
                setTargets(ctx, 1, one, IntPtr.Zero);
                var vp = new Viewport { Width = lumWidth, Height = lumHeight, MaxDepth = 1 };
                setViewports(ctx, 1, ref vp);
                setPS(ctx, psLum, IntPtr.Zero, 0);
                draw(ctx, 3, 0);

                // Its blurred copies, then pass 2 to the screen.
                one[0] = rtv;
                setTargets(ctx, 1, one, IntPtr.Zero);
                generateMips(ctx, lumSrv);
                vp = new Viewport { Width = width, Height = height, MaxDepth = 1 };
                setViewports(ctx, 1, ref vp);
                setPS(ctx, ps, IntPtr.Zero, 0);
                two[1] = lumSrv;
                setResources(ctx, 0, 2, two);
                one[0] = sampler;
                setSamplers(ctx, 0, 1, one);
                draw(ctx, 3, 0);

                if (owner.TestPoints != null) Sample(owner);
                if (owner.DumpDir != null) Dump(owner);
                present(swap, 0, 0);
            }

            // Diagnostics: saves the same area of the desktop as captured and as shown, as PNG files.
            void Dump(SmartFilter owner)
            {
                if (stageIn == IntPtr.Zero) stageIn = Staging(source);
                if (stageOut == IntPtr.Zero) stageOut = Staging(backBuffer);
                copy(ctx, stageIn, source);
                copy(ctx, stageOut, backBuffer);
                SaveCrop(stageIn, owner.DumpRect, System.IO.Path.Combine(owner.DumpDir, "input.png"));
                SaveCrop(stageOut, owner.DumpRect, System.IO.Path.Combine(owner.DumpDir, "output.png"));
                owner.DumpDir = null;
            }

            void SaveCrop(IntPtr staging, Rectangle r, string path)
            {
                Mapped m;
                if (Fn<MapFn>(ctx, 14)(ctx, staging, 0, 1, 0, out m) < 0) return;
                using (var bmp = new Bitmap(r.Width, r.Height, System.Drawing.Imaging.PixelFormat.Format32bppRgb))
                {
                    var data = bmp.LockBits(new Rectangle(0, 0, r.Width, r.Height), System.Drawing.Imaging.ImageLockMode.WriteOnly, bmp.PixelFormat);
                    var row = new byte[r.Width * 4];
                    for (int y = 0; y < r.Height; y++)
                    {
                        Marshal.Copy(m.Data + (r.Y + y) * (int)m.RowPitch + r.X * 4, row, 0, row.Length);
                        Marshal.Copy(row, 0, data.Scan0 + y * data.Stride, row.Length);
                    }
                    bmp.UnlockBits(data);
                    Fn<UnmapFn>(ctx, 15)(ctx, staging, 0);
                    bmp.Save(path, System.Drawing.Imaging.ImageFormat.Png);
                }
            }

            void Sample(SmartFilter owner)
            {
                if (stageIn == IntPtr.Zero) stageIn = Staging(source);
                if (stageOut == IntPtr.Zero) stageOut = Staging(backBuffer);
                copy(ctx, stageIn, source);
                copy(ctx, stageOut, backBuffer);
                int n = owner.TestPoints.Length;
                int[] input = new int[n], outputPx = new int[n];
                Read(stageIn, owner.TestPoints, input);
                Read(stageOut, owner.TestPoints, outputPx);
                owner.TestIn = input;
                owner.TestOut = outputPx;
                owner.TestFrames++;
            }

            IntPtr Staging(IntPtr like)
            {
                TexDesc d;
                Fn<TexGetDescFn>(like, 10)(like, out d);
                d.MipLevels = 1; d.ArraySize = 1; d.Usage = 3; d.BindFlags = 0; d.CpuAccess = 0x20000; d.Misc = 0;   // staging, CPU read
                IntPtr t;
                Check(Fn<CreateTexture2DFn>(device, 5)(device, ref d, IntPtr.Zero, out t), "Creating a staging texture");
                return t;
            }

            void Read(IntPtr staging, Point[] points, int[] into)
            {
                Mapped m;
                if (Fn<MapFn>(ctx, 14)(ctx, staging, 0, 1, 0, out m) < 0) return;   // MAP_READ
                for (int i = 0; i < points.Length; i++)
                    into[i] = Marshal.ReadInt32(m.Data, points[i].Y * (int)m.RowPitch + points[i].X * 4);
                Fn<UnmapFn>(ctx, 15)(ctx, staging, 0);
            }

            static void Release(ref IntPtr p)
            {
                if (p != IntPtr.Zero) { Marshal.Release(p); p = IntPtr.Zero; }
            }

            public void Dispose()
            {
                Release(ref stagePeak); Release(ref peakRtv); Release(ref peakTex); Release(ref psPeak);
                Release(ref stageMap); Release(ref stageOut); Release(ref stageIn); Release(ref srv); Release(ref source);
                Release(ref lumSrv); Release(ref lumRtv); Release(ref lumTex); Release(ref sampler); Release(ref psLum);
                Release(ref cb); Release(ref ps); Release(ref vs); Release(ref rtv); Release(ref backBuffer);
                Release(ref swap); Release(ref dup); Release(ref output1); Release(ref ctx); Release(ref device);
                Release(ref output); Release(ref adapter); Release(ref factory);
                if (metadata != IntPtr.Zero) { Marshal.FreeHGlobal(metadata); metadata = IntPtr.Zero; }
            }
        }
    }
}
