using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace BlinkBuddy
{
    // One-click screen modes, and modes chosen per app.
    // A mode is a set of screen settings: Pause turns the filter off; Health, Game, Movie, Office, Editing and Reading
    // set brightness, the blue light filter and the colour effect; Custom is your own values, kept aside while another
    // mode is in use and brought back when you pick Custom again (or move a slider yourself).
    // A rule ("code.exe" -> Editing) applies its mode while that app is in front, to a copy of the settings, so your
    // own values are untouched and come back when you switch away. The night schedule leaves it as it is.
    static class Modes
    {
        public const int Pause = 0, Health = 1, Game = 2, Movie = 3, Office = 4, Editing = 5, Reading = 6, Custom = 7;
        public static readonly string[] Names = { "Pause", "Health", "Game", "Movie", "Office", "Editing", "Reading", "Custom" };
        // Segoe Fluent Icons / MDL2: pause, heart, game pad, film, briefcase, pen, book, sliders
        public static readonly string[] Glyphs = { "", "", "", "", "", "", "", "" };

        // brightness %, blue light filter %, colour effect
        static readonly int[,] Presets =
        {
            { 100, 0, 0 },    // Pause (the filter is switched off)
            { 75, 65, 0 },    // Health: dimmer and warm, for long sessions
            { 100, 10, 0 },   // Game: bright, a touch warm
            { 90, 25, 0 },    // Movie
            { 90, 35, 0 },    // Office
            { 100, 0, 0 },    // Editing: true colour (and no tone curve)
            { 85, 50, 0 }     // Reading: warm, a little dimmer
        };

        // Puts a mode's values into a settings object (Custom: the values kept aside).
        public static void Apply(Settings s, int mode)
        {
            if (mode == Custom)
            {
                s.FilterEnabled = s.KeptFilter;
                s.Brightness = s.KeptBrightness;
                s.BlueFilter = s.CoolRange ? s.KeptBlue : Math.Max(0, s.KeptBlue);   // (the cool side may have been switched off since)
                s.ColorEffect = s.KeptEffect;
                s.ToneEnabled = s.KeptTone;
                return;
            }
            if (mode < 0 || mode >= Custom) return;
            s.FilterEnabled = mode != Pause;
            if (mode == Pause) return;
            s.Brightness = Presets[mode, 0];
            s.BlueFilter = Presets[mode, 1];
            s.ColorEffect = Presets[mode, 2];
            // Editing: true colour; the others keep your own tone setting (the live one while your own values show, as
            // for a rule applied over Custom; otherwise the one kept aside)
            bool ownTone = s.Mode == Custom || s.Mode < 0 ? s.ToneEnabled : s.KeptTone;
            s.ToneEnabled = mode != Editing && ownTone;
        }

        // Picking a mode by hand: your own values are put aside first (if they are what's showing), then the mode
        // goes into the settings themselves.
        public static void Choose(Settings s, int mode)
        {
            if (s.Mode == Custom || s.Mode < 0) KeepCustom(s);
            s.Mode = mode;
            Apply(s, mode);
        }

        public static void KeepCustom(Settings s)
        {
            s.KeptFilter = s.FilterEnabled;
            s.KeptBrightness = s.Brightness;
            s.KeptBlue = s.BlueFilter;
            s.KeptEffect = s.ColorEffect;
            s.KeptTone = s.ToneEnabled;
        }

        // ---- Rules: "photoshop.exe=0=C:\...\Photoshop.exe|code.exe=5" (the path, when known, gives the name and icon) ----

        public struct Rule { public string App; public int Mode; public string Path; }

        public static List<Rule> Rules(Settings s)
        {
            var list = new List<Rule>();
            foreach (string part in (s.AppRules ?? "").Split('|'))
            {
                string[] bits = part.Split(new[] { '=' }, 3);
                int m;
                if (bits.Length < 2 || bits[0].Trim().Length == 0 || !int.TryParse(bits[1], out m)) continue;
                list.Add(new Rule { App = bits[0].Trim().ToLowerInvariant(), Mode = m, Path = bits.Length > 2 ? bits[2] : "" });
            }
            return list;
        }

        public static void SaveRules(Settings s, List<Rule> rules)
        {
            var sb = new StringBuilder();
            foreach (Rule r in rules)
            {
                if (sb.Length > 0) sb.Append('|');
                sb.Append(r.App).Append('=').Append(r.Mode);
                if (!string.IsNullOrEmpty(r.Path)) sb.Append('=').Append(r.Path);
            }
            s.AppRules = sb.ToString();
        }

        public static int RuleFor(Settings s, string app)
        {
            if (string.IsNullOrEmpty(app)) return -1;
            foreach (Rule r in Rules(s)) if (r.App == app) return r.Mode;
            return -1;
        }

        // ---- The app in front ----

        [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
        [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
        [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hwnd);
        [DllImport("user32.dll")] static extern bool IsIconic(IntPtr hwnd);
        [DllImport("user32.dll")] static extern bool IsWindow(IntPtr hwnd);
        [DllImport("user32.dll")] static extern IntPtr GetWindow(IntPtr hwnd, uint cmd);
        [DllImport("user32.dll")] static extern int GetWindowLong(IntPtr hwnd, int index);
        [DllImport("user32.dll")] static extern int GetWindowTextLength(IntPtr hwnd);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int max);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr hwnd, StringBuilder name, int max);
        delegate bool EnumProc(IntPtr hwnd, IntPtr data);
        [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc proc, IntPtr data);
        [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent, EnumProc proc, IntPtr data);
        [DllImport("dwmapi.dll")] static extern int DwmGetWindowAttribute(IntPtr hwnd, int attr, out int value, int size);
        [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
        [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern bool QueryFullProcessImageName(IntPtr h, int flags, StringBuilder name, ref int size);
        static readonly Dictionary<uint, string> names = new Dictionary<uint, string>();
        static readonly uint me = (uint)Process.GetCurrentProcess().Id;

        // Windows that only pass through the front - the taskbar, Start, search, Alt+Tab, the desktop, notifications, menus -
        // and BlinkBuddy itself. While one of them is in front, the app before it still counts.
        static readonly string[] PassingClasses =
        {
            "Shell_TrayWnd", "Shell_SecondaryTrayWnd", "Progman", "WorkerW", "Windows.UI.Core.CoreWindow", "MultitaskingViewFrame",
            "XamlExplorerHostIslandWindow", "TaskListThumbnailWnd", "NotifyIconOverflowWindow", "TopLevelWindowForOverflowXamlIsland",
            "ForegroundStaging", "#32768", "tooltips_class32"
        };
        static readonly string[] PassingApps =
        {
            "shellexperiencehost.exe", "startmenuexperiencehost.exe", "searchhost.exe", "searchapp.exe", "lockapp.exe",
            "shellhost.exe", "textinputhost.exe"
        };

        // The app in front, as "name.exe" in lower case (null when there is none), and its window. False when the window in
        // front only passes through (see above): then whatever applied before should go on applying.
        public static bool ForegroundApp(out string app, out IntPtr window)
        {
            window = GetForegroundWindow();
            app = null;
            if (window == IntPtr.Zero) return false;
            if (Array.IndexOf(PassingClasses, ClassOf(window)) >= 0) return false;
            uint pid = AppPid(window);
            if (pid == 0 || pid == me) return false;
            app = NameOf(pid);
            return app == null || Array.IndexOf(PassingApps, app) < 0;
        }

        // Whether a window is still there to be seen (not closed, hidden or minimised).
        public static bool StillShown(IntPtr window)
        {
            return window != IntPtr.Zero && IsWindow(window) && IsWindowVisible(window) && !IsIconic(window);
        }

        // The process a window belongs to; for a Store app (whose frame belongs to ApplicationFrameHost), the app inside it.
        static uint AppPid(IntPtr window)
        {
            uint pid;
            GetWindowThreadProcessId(window, out pid);
            if (NameOf(pid) != "applicationframehost.exe") return pid;
            uint inner = 0;
            EnumChildWindows(window, (child, data) =>
            {
                uint p;
                GetWindowThreadProcessId(child, out p);
                if (p == pid) return true;
                inner = p;
                return false;
            }, IntPtr.Zero);
            return inner != 0 ? inner : pid;
        }

        static string NameOf(uint pid)
        {
            string name;
            if (names.TryGetValue(pid, out name)) return name;
            try { using (Process p = Process.GetProcessById((int)pid)) name = p.ProcessName.ToLowerInvariant() + ".exe"; }
            catch { name = null; }
            if (names.Count > 500) names.Clear();   // process ids get reused; keep it small
            names[pid] = name;
            return name;
        }

        static string ClassOf(IntPtr window)
        {
            var sb = new StringBuilder(128);
            GetClassName(window, sb, sb.Capacity);
            return sb.ToString();
        }

        // A program's file, by process id (works for programs running as administrator too); "" if it can't be read.
        static string PathOf(uint pid)
        {
            IntPtr h = OpenProcess(0x1000, false, pid);   // PROCESS_QUERY_LIMITED_INFORMATION
            if (h == IntPtr.Zero) return "";
            try
            {
                var sb = new StringBuilder(1024);
                int size = sb.Capacity;
                return QueryFullProcessImageName(h, 0, sb, ref size) ? sb.ToString(0, size) : "";
            }
            finally { CloseHandle(h); }
        }

        public class AppInfo { public string App, Title, Path; }

        // Apps with a window you can see on the taskbar right now (to pick from when adding a rule), with their window
        // titles. Not: hidden or cloaked windows, tool and overlay windows (e.g. a GPU overlay), owned dialogs, the shell.
        public static List<AppInfo> RunningApps()
        {
            var seen = new HashSet<string>();
            var list = new List<AppInfo>();
            EnumWindows((w, data) =>
            {
                try
                {
                    if (!IsWindowVisible(w) || GetWindowTextLength(w) == 0 || GetWindow(w, 4) != IntPtr.Zero) return true;   // GW_OWNER
                    int ex = GetWindowLong(w, -20);   // GWL_EXSTYLE
                    bool appWindow = (ex & 0x40000) != 0;   // WS_EX_APPWINDOW
                    if (!appWindow && (ex & (0x80 | 0x08000000 | 0x20)) != 0) return true;   // tool, no-activate or click-through
                    int cloaked;
                    if (DwmGetWindowAttribute(w, 14, out cloaked, 4) == 0 && cloaked != 0) return true;   // DWMWA_CLOAKED
                    if (Array.IndexOf(PassingClasses, ClassOf(w)) >= 0) return true;
                    uint pid = AppPid(w);
                    string name = NameOf(pid);
                    if (pid == me || name == null || Array.IndexOf(PassingApps, name) >= 0 || name == "applicationframehost.exe") return true;
                    if (!seen.Add(name)) return true;
                    var title = new StringBuilder(256);
                    GetWindowText(w, title, title.Capacity);
                    list.Add(new AppInfo { App = name, Title = title.ToString(), Path = PathOf(pid) });
                }
                catch { }
                return true;
            }, IntPtr.Zero);
            list.Sort((a, b) => string.Compare(NameOf(a.App, a.Path), NameOf(b.App, b.Path), StringComparison.CurrentCultureIgnoreCase));
            return list;
        }

        // Tells when another window comes to the front, at once (rather than on the next second's look).
        public sealed class FrontWatch : IDisposable
        {
            delegate void WinEventProc(IntPtr hook, uint e, IntPtr hwnd, int idObject, int idChild, uint thread, uint time);
            [DllImport("user32.dll")] static extern IntPtr SetWinEventHook(uint min, uint max, IntPtr module, WinEventProc proc, uint pid, uint thread, uint flags);
            [DllImport("user32.dll")] static extern bool UnhookWinEvent(IntPtr hook);
            readonly WinEventProc proc;   // held in a field so the GC can't collect it while Windows calls it
            IntPtr hook;

            public FrontWatch(Action changed)
            {
                proc = (h, e, hwnd, obj, child, thread, time) => changed();
                // EVENT_SYSTEM_FOREGROUND, out of context (our own windows too: the settings window counts)
                hook = SetWinEventHook(0x3, 0x3, IntPtr.Zero, proc, 0, 0, 0x0);
            }

            public void Dispose()
            {
                if (hook != IntPtr.Zero) UnhookWinEvent(hook);
                hook = IntPtr.Zero;
            }
        }

        // A program's own name ("Google Chrome"), or its file name without ".exe".
        public static string NameOf(string app, string path)
        {
            try
            {
                if (!string.IsNullOrEmpty(path) && File.Exists(path))
                {
                    string d = FileVersionInfo.GetVersionInfo(path).FileDescription;
                    if (!string.IsNullOrWhiteSpace(d)) return d.Trim();
                }
            }
            catch { }
            string n = app.EndsWith(".exe") ? app.Substring(0, app.Length - 4) : app;
            return n.Length > 0 ? char.ToUpper(n[0]) + n.Substring(1) : n;
        }

        // A program's icon at a size in pixels, or null.
        public static Bitmap IconOf(string path, int size)
        {
            try
            {
                if (string.IsNullOrEmpty(path) || !File.Exists(path)) return null;
                using (Icon icon = Icon.ExtractAssociatedIcon(path))
                using (Bitmap big = icon.ToBitmap())
                {
                    var b = new Bitmap(size, size);
                    using (Graphics g = Graphics.FromImage(b))
                    {
                        g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                        g.DrawImage(big, 0, 0, size, size);
                    }
                    return b;
                }
            }
            catch { return null; }
        }
    }

    // Keeping the PC awake: Windows is told the system and the display are in use, so it neither sleeps nor turns the
    // screen off, until this is switched off again (or BlinkBuddy exits).
    static class KeepAwake
    {
        [DllImport("kernel32.dll")] static extern uint SetThreadExecutionState(uint flags);
        const uint Continuous = 0x80000000, System = 0x1, Display = 0x2;
        static bool on;

        public static void Set(bool value)
        {
            if (value == on) return;
            on = value;
            SetThreadExecutionState(value ? Continuous | System | Display : Continuous);
        }
    }
}
