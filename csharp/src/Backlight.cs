using System;
using System.Collections.Generic;
using System.Management;
using System.Threading;

namespace BlinkBuddy
{
    // Sets the real backlight: external monitors over DDC/CI, a laptop's built-in panel through WMI. Each round-trip
    // takes tens of milliseconds, so work runs on a pool thread and rapid slider moves collapse into the latest value.
    static class Backlight
    {
        static readonly object gate = new object(), bus = new object();
        static int pending = -1;
        static bool busy;

        public static void Set(int percent)
        {
            lock (gate)
            {
                pending = percent;
                if (busy) return;
                busy = true;
            }
            ThreadPool.QueueUserWorkItem(delegate { Drain(); });
        }

        static void Drain()
        {
            while (true)
            {
                int p;
                lock (gate)
                {
                    p = pending;
                    pending = -1;
                    if (p < 0) { busy = false; return; }
                }
                ForEachMonitor((h, min, cur, max) =>
                    Native.SetMonitorBrightness(h, (uint)(min + Math.Round((max - min) * p / 100.0))));
                SetLaptopPanel(p);
            }
        }

        // Reports (displays that answered, displays in total, first answering display's brightness %) on a pool thread.
        public static void Probe(Action<int, int, int> done)
        {
            ThreadPool.QueueUserWorkItem(delegate
            {
                int supported = 0, first = -1;
                int total = ForEachMonitor((h, min, cur, max) =>
                {
                    supported++;
                    if (first < 0) first = max > min ? (int)Math.Round(100.0 * (cur - min) / (max - min)) : 100;
                });
                int panel = LaptopPanelBrightness();
                if (panel >= 0)
                {
                    supported++;
                    if (first < 0) first = panel;
                }
                done(supported, Math.Max(total, supported), first);
            });
        }

        // Built-in laptop screens don't speak DDC/CI; Windows exposes them through WMI instead.
        static int LaptopPanelBrightness()
        {
            try
            {
                using (var searcher = new ManagementObjectSearcher(@"root\WMI", "SELECT CurrentBrightness FROM WmiMonitorBrightness"))
                    foreach (ManagementObject o in searcher.Get()) return Convert.ToInt32(o["CurrentBrightness"]);
            }
            catch { }
            return -1;
        }

        static void SetLaptopPanel(int percent)
        {
            try
            {
                using (var searcher = new ManagementObjectSearcher(@"root\WMI", "SELECT * FROM WmiMonitorBrightnessMethods"))
                    foreach (ManagementObject o in searcher.Get()) o.InvokeMethod("WmiSetBrightness", new object[] { (uint)1, (byte)percent });
            }
            catch { }
        }

        // Calls act for every physical monitor that reports a brightness range; returns how many physical monitors exist.
        static int ForEachMonitor(Action<IntPtr, uint, uint, uint> act)
        {
            lock (bus)
            {
                var handles = new List<IntPtr>();
                Native.EnumDisplayMonitors(IntPtr.Zero, IntPtr.Zero, (hm, hdc, rect, data) => { handles.Add(hm); return true; }, IntPtr.Zero);
                int total = 0;
                foreach (IntPtr hm in handles)
                {
                    uint n;
                    if (!Native.GetNumberOfPhysicalMonitorsFromHMONITOR(hm, out n) || n == 0) continue;
                    var monitors = new Native.PHYSICAL_MONITOR[n];
                    if (!Native.GetPhysicalMonitorsFromHMONITOR(hm, n, monitors)) continue;
                    total += (int)n;
                    foreach (Native.PHYSICAL_MONITOR m in monitors)
                    {
                        uint min, cur, max;
                        if (Native.GetMonitorBrightness(m.hPhysicalMonitor, out min, out cur, out max)) act(m.hPhysicalMonitor, min, cur, max);
                    }
                    Native.DestroyPhysicalMonitors(n, monitors);
                }
                return total;
            }
        }
    }
}
