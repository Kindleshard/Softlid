using System;
using System.Drawing;
using System.Runtime.InteropServices;

namespace BlinkBuddy
{
    static class Native
    {
        public const int WS_EX_TOPMOST = 0x8, WS_EX_TRANSPARENT = 0x20, WS_EX_TOOLWINDOW = 0x80,
                         WS_EX_LAYERED = 0x80000, WS_EX_NOACTIVATE = 0x8000000;
        public const int MOD_ALT = 0x1, MOD_CONTROL = 0x2, MOD_SHIFT = 0x4, MOD_NOREPEAT = 0x4000;

        // --- Magnification API: a full-screen colour matrix applied by the compositor ---
        [StructLayout(LayoutKind.Sequential)]
        public struct MAGCOLOREFFECT
        {
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 25)]
            public float[] transform;
        }
        [DllImport("Magnification.dll")] public static extern bool MagInitialize();
        [DllImport("Magnification.dll")] public static extern bool MagUninitialize();
        [DllImport("Magnification.dll")] public static extern bool MagSetFullscreenColorEffect(ref MAGCOLOREFFECT effect);

        // --- Per-pixel-alpha layered windows ---
        [StructLayout(LayoutKind.Sequential)] struct POINT { public int X, Y; }
        [StructLayout(LayoutKind.Sequential)] struct SIZE { public int Cx, Cy; }
        [StructLayout(LayoutKind.Sequential, Pack = 1)] struct BLENDFUNCTION { public byte BlendOp, BlendFlags, SourceConstantAlpha, AlphaFormat; }
        [DllImport("user32.dll")] static extern bool UpdateLayeredWindow(IntPtr hwnd, IntPtr hdcDst, ref POINT pptDst, ref SIZE psize, IntPtr hdcSrc, ref POINT pptSrc, int crKey, ref BLENDFUNCTION pblend, int flags);
        [DllImport("user32.dll")] static extern IntPtr GetDC(IntPtr hwnd);
        [DllImport("user32.dll")] static extern int ReleaseDC(IntPtr hwnd, IntPtr hdc);
        [DllImport("gdi32.dll")] static extern IntPtr CreateCompatibleDC(IntPtr hdc);
        [DllImport("gdi32.dll")] static extern bool DeleteDC(IntPtr hdc);
        [DllImport("gdi32.dll")] static extern IntPtr SelectObject(IntPtr hdc, IntPtr obj);
        [DllImport("gdi32.dll")] static extern bool DeleteObject(IntPtr obj);

        public static void UpdateLayered(IntPtr hwnd, Bitmap bmp, Point at, byte alpha)
        {
            IntPtr screen = GetDC(IntPtr.Zero), mem = CreateCompatibleDC(screen);
            IntPtr hbmp = bmp.GetHbitmap(Color.FromArgb(0)), old = SelectObject(mem, hbmp);
            try
            {
                var dst = new POINT { X = at.X, Y = at.Y };
                var src = new POINT();
                var size = new SIZE { Cx = bmp.Width, Cy = bmp.Height };
                var blend = new BLENDFUNCTION { SourceConstantAlpha = alpha, AlphaFormat = 1 }; // AC_SRC_OVER, AC_SRC_ALPHA
                UpdateLayeredWindow(hwnd, screen, ref dst, ref size, mem, ref src, 0, ref blend, 2); // ULW_ALPHA
            }
            finally
            {
                SelectObject(mem, old);
                DeleteObject(hbmp);
                DeleteDC(mem);
                ReleaseDC(IntPtr.Zero, screen);
            }
        }

        // --- Windows ---
        [DllImport("user32.dll")] static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int cx, int cy, int flags);
        public static void BringToTopNoActivate(IntPtr hwnd)
        {
            SetWindowPos(hwnd, new IntPtr(-1), 0, 0, 0, 0, 0x1 | 0x2 | 0x10); // HWND_TOPMOST; NOSIZE | NOMOVE | NOACTIVATE
        }
        [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, int msg, IntPtr w, IntPtr l);
        [DllImport("user32.dll")] public static extern bool RegisterHotKey(IntPtr hwnd, int id, int mods, int vk);
        [DllImport("user32.dll")] public static extern bool UnregisterHotKey(IntPtr hwnd, int id);
        [DllImport("user32.dll")] public static extern bool AllowSetForegroundWindow(int pid);
        [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
        [DllImport("wtsapi32.dll")] public static extern bool WTSRegisterSessionNotification(IntPtr hwnd, int flags);
        [DllImport("wtsapi32.dll")] public static extern bool WTSUnRegisterSessionNotification(IntPtr hwnd);
        [DllImport("dwmapi.dll")] static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int value, int size);
        public static void DarkTitleBar(IntPtr hwnd) { int on = 1; DwmSetWindowAttribute(hwnd, 20, ref on, 4); }
        public static void RoundCorners(IntPtr hwnd) { int round = 2; DwmSetWindowAttribute(hwnd, 33, ref round, 4); }

        [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr hwnd, int msg, IntPtr w, string l);
        [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hwnd, int msg, IntPtr w, IntPtr l);
        [DllImport("user32.dll")] public static extern bool SetLayeredWindowAttributes(IntPtr hwnd, uint key, byte alpha, uint flags);
        [DllImport("user32.dll")] public static extern bool SetWindowDisplayAffinity(IntPtr hwnd, uint affinity);
        public static void MoveNoActivate(IntPtr hwnd, int x, int y)
        {
            SetWindowPos(hwnd, IntPtr.Zero, x, y, 0, 0, 0x1 | 0x4 | 0x10); // NOSIZE | NOZORDER | NOACTIVATE
        }

        // Limits a window's visible area to r (window coordinates).
        public static void ClipWindow(IntPtr hwnd, Rectangle r)
        {
            SetWindowRgn(hwnd, CreateRectRgn(r.Left, r.Top, r.Right, r.Bottom), true); // the system owns the region
        }

        public static void PlaceTopmost(IntPtr hwnd, Rectangle r)
        {
            SetWindowPos(hwnd, new IntPtr(-1), r.X, r.Y, r.Width, r.Height, 0x10); // NOACTIVATE
        }

        // --- Window regions (reading strip) ---
        [DllImport("gdi32.dll")] static extern IntPtr CreateRectRgn(int l, int t, int r, int b);
        [DllImport("user32.dll")] static extern int SetWindowRgn(IntPtr hwnd, IntPtr rgn, bool redraw);
        [DllImport("gdi32.dll")] static extern IntPtr CreateEllipticRgn(int l, int t, int r, int b);
        [DllImport("gdi32.dll")] static extern IntPtr CreateRoundRectRgn(int l, int t, int r, int b, int w, int h);
        [DllImport("gdi32.dll")] static extern int CombineRgn(IntPtr dest, IntPtr a, IntPtr b, int mode);

        // Shows only `keep` minus a hole (window coordinates): 0 a rectangle, 1 a rectangle rounded by `radius`, 2 an
        // ellipse. One call, so the window changes in a single step.
        public static void CutHole(IntPtr hwnd, Rectangle keep, Rectangle hole, int kind, int radius)
        {
            IntPtr r = CreateRectRgn(keep.Left, keep.Top, keep.Right, keep.Bottom);
            if (hole.Width > 0 && hole.Height > 0)
            {
                IntPtr h = kind == 2 ? CreateEllipticRgn(hole.Left, hole.Top, hole.Right + 1, hole.Bottom + 1)
                    : kind == 1 ? CreateRoundRectRgn(hole.Left, hole.Top, hole.Right + 1, hole.Bottom + 1, radius * 2, radius * 2)
                    : CreateRectRgn(hole.Left, hole.Top, hole.Right, hole.Bottom);
                CombineRgn(r, r, h, 4);   // RGN_DIFF
                DeleteObject(h);
            }
            SetWindowRgn(hwnd, r, true);   // the system owns the region
        }

        public static void ClearRegion(IntPtr hwnd) { SetWindowRgn(hwnd, IntPtr.Zero, true); }

        // --- Low-level mouse hook (reading strip) ---
        public delegate IntPtr HookProc(int code, IntPtr w, IntPtr l);
        [DllImport("user32.dll")] public static extern IntPtr SetWindowsHookEx(int id, HookProc proc, IntPtr mod, int thread);
        [DllImport("user32.dll")] public static extern bool UnhookWindowsHookEx(IntPtr hook);
        [DllImport("user32.dll")] public static extern IntPtr CallNextHookEx(IntPtr hook, int code, IntPtr w, IntPtr l);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr GetModuleHandle(string name);

        // --- DDC/CI monitor backlight ---
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        public struct PHYSICAL_MONITOR
        {
            public IntPtr hPhysicalMonitor;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string szPhysicalMonitorDescription;
        }
        public delegate bool MonitorEnumProc(IntPtr hMonitor, IntPtr hdc, IntPtr rect, IntPtr data);
        [DllImport("user32.dll")] public static extern bool EnumDisplayMonitors(IntPtr hdc, IntPtr clip, MonitorEnumProc proc, IntPtr data);
        [DllImport("dxva2.dll")] public static extern bool GetNumberOfPhysicalMonitorsFromHMONITOR(IntPtr hMonitor, out uint count);
        [DllImport("dxva2.dll")] public static extern bool GetPhysicalMonitorsFromHMONITOR(IntPtr hMonitor, uint count, [Out] PHYSICAL_MONITOR[] monitors);
        [DllImport("dxva2.dll")] public static extern bool DestroyPhysicalMonitors(uint count, PHYSICAL_MONITOR[] monitors);
        [DllImport("dxva2.dll")] public static extern bool GetMonitorBrightness(IntPtr monitor, out uint min, out uint current, out uint max);
        [DllImport("dxva2.dll")] public static extern bool SetMonitorBrightness(IntPtr monitor, uint value);

        // --- Activity ---
        [StructLayout(LayoutKind.Sequential)] struct LASTINPUTINFO { public int cbSize; public uint dwTime; }
        [DllImport("user32.dll")] static extern bool GetLastInputInfo(ref LASTINPUTINFO lii);
        public static int IdleSeconds()
        {
            var lii = new LASTINPUTINFO { cbSize = Marshal.SizeOf(typeof(LASTINPUTINFO)) };
            if (!GetLastInputInfo(ref lii)) return 0;
            return (int)(((uint)Environment.TickCount - lii.dwTime) / 1000);
        }

        [DllImport("shell32.dll")] static extern int SHQueryUserNotificationState(out int state);
        [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
        [DllImport("user32.dll")] static extern IntPtr GetShellWindow();
        [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
        [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr hwnd, out RECT r);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr hwnd, System.Text.StringBuilder name, int max);
        [StructLayout(LayoutKind.Sequential)] struct RECT { public int Left, Top, Right, Bottom; }

        public static bool FullscreenAppRunning()
        {
            // Exclusive full-screen games and presentation mode, as Windows reports them.
            int state;
            if (SHQueryUserNotificationState(out state) == 0 && (state == 3 || state == 4)) return true;
            // Windows' "busy" state also counts any always-on-top window covering a screen, BlinkBuddy's own overlays
            // included (Smart filters, grain, reading strip), which paused the reminders for good. So judge the window
            // in use instead: someone else's window covering its whole screen (a video, a borderless game, F11).
            IntPtr fg = GetForegroundWindow();
            if (fg == IntPtr.Zero || fg == GetShellWindow()) return false;
            uint pid;
            GetWindowThreadProcessId(fg, out pid);
            if (pid == (uint)System.Diagnostics.Process.GetCurrentProcess().Id) return false;
            var cls = new System.Text.StringBuilder(64);
            GetClassName(fg, cls, 64);
            if (cls.ToString() == "WorkerW" || cls.ToString() == "Progman") return false;   // the desktop
            RECT r;
            if (!GetWindowRect(fg, out r)) return false;
            Rectangle screen = System.Windows.Forms.Screen.FromHandle(fg).Bounds;
            return r.Left <= screen.Left && r.Top <= screen.Top && r.Right >= screen.Right && r.Bottom >= screen.Bottom;
        }

        // --- Memory ---
        [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();
        [DllImport("kernel32.dll")] static extern bool SetProcessWorkingSetSize(IntPtr proc, IntPtr min, IntPtr max);
        public static void TrimWorkingSet() { SetProcessWorkingSetSize(GetCurrentProcess(), new IntPtr(-1), new IntPtr(-1)); }
    }
}
