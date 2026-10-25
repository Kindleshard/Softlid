using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace BlinkBuddy
{
    // Reshapes brightness levels at the display output: darker, tighter shadows; highlights squeezed toward the
    // darks; and a cap on the brightest whites. Windows refuses a gamma curve that strays more than half the range
    // from normal, so strong settings are split into two stages:
    //   1. the colour effect scales the whole image down by Prescale while compositing (no strength limit);
    //   2. the gamma curve, applied by the graphics card at scan-out, lifts darks and mid-tones back where they belong.
    // Both are per-frame lookups with nothing to detect, so a white page is tamed on its very first frame.
    class ToneMapper : IDisposable
    {
        const double MaxDeviation = 0.45;   // stay a little inside Windows' limit of 0.5

        [DllImport("gdi32.dll", CharSet = CharSet.Unicode)] static extern IntPtr CreateDC(string driver, string device, string output, IntPtr init);
        [DllImport("gdi32.dll")] static extern bool DeleteDC(IntPtr dc);
        [DllImport("gdi32.dll")] static extern bool GetDeviceGammaRamp(IntPtr dc, [Out] ushort[] ramp);
        [DllImport("gdi32.dll")] static extern bool SetDeviceGammaRamp(IntPtr dc, ushort[] ramp);

        readonly Dictionary<string, ushort[]> originals = new Dictionary<string, ushort[]>();
        readonly Dictionary<string, ushort[]> applied = new Dictionary<string, ushort[]>();
        float prescale = 1f;
        public string Status = "";

        public float Prescale { get { return prescale; } }
        public bool Active { get { return applied.Count > 0; } }

        // Exists while one of our gamma curves is on screen, so a crash can be cleaned up on the next start.
        static string Marker
        {
            get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "BlinkBuddy", "tone-active"); }
        }

        // 0% = no cap, 100% = whites at 20%.
        public static double CapFor(int limit) { return 1 - 0.8 * limit / 100.0; }
        // The whole curve: input brightness 0..1 -> what is shown.
        public static double Net(double x, int shadows, int squeeze, int limit)
        {
            return Limit(Squeeze(Shadows(x, shadows / 100.0), squeeze / 100.0), CapFor(limit));
        }

        // Darks get darker and bunch up toward black: a power curve that blends back to normal by mid-grey.
        static double Shadows(double x, double s)
        {
            if (s <= 0) return x;
            double w = Smooth(x / 0.5);
            return Math.Pow(x, 1 + 1.5 * s) * (1 - w) + x * w;
        }

        // Everything above a quarter brightness is compressed toward it (down to 15% of its spread), easing in smoothly.
        static double Squeeze(double x, double s)
        {
            if (s <= 0) return x;
            const double anchor = 0.25, ease = 0.2;
            double u = x - anchor, excess;
            if (u <= 0) excess = 0;
            else if (u < ease) { double t = u / ease; excess = ease * (t * t * t - t * t * t * t / 2); }   // integral of smoothstep
            else excess = u - ease / 2;
            return x - 0.85 * s * excess;
        }

        // Untouched up to a knee, then an exponential shoulder that leaves the knee at slope ~1 and reaches the cap at full input.
        static double Limit(double x, double cap)
        {
            if (cap >= 1) return x;
            double knee = cap >= 0.5 ? (3 * cap - 1) / 2 : cap / 2, span = cap - knee;
            if (x <= knee) return x;
            return knee + span * (1 - Math.Exp(-(x - knee) / span)) / (1 - Math.Exp(-(1 - knee) / span));
        }

        static double Smooth(double t) { return t <= 0 ? 0 : t >= 1 ? 1 : t * t * (3 - 2 * t); }

        // Gamma lookup for pre-scale d: undo the pre-scale, then apply the curve. Above d (only reached by things the
        // colour effect doesn't touch, such as some cursors) it keeps climbing with slope 1.
        static double Lookup(double y, double d, Func<double, double> f)
        {
            return y <= d ? f(y / d) : Math.Min(1, f(1) + (y - d));
        }

        static double WorstDeviation(double d, Func<double, double> f)
        {
            double worst = 0;
            for (int i = 0; i < 256; i++)
            {
                double y = i / 255.0;
                worst = Math.Max(worst, Math.Abs(Lookup(y, d, f) - y));
            }
            return worst;
        }

        public void Set(int shadows, int squeeze, int limit)
        {
            if (shadows == 0 && squeeze == 0 && limit == 0) Off();
            else SetCurve(x => Net(x, shadows, squeeze, limit));
        }

        public void Off()
        {
            Restore();
            prescale = 1f;
            Status = "";
        }

        // Any monotonic curve with f(0) = 0; split into colour-effect pre-scale and gamma lookup as needed.
        public void SetCurve(Func<double, double> f)
        {
            // Use the least pre-scaling (best precision in dark gradients) whose gamma curve stays inside the limit;
            // if Windows still refuses, retry with a tighter limit.
            foreach (double allowed in new[] { MaxDeviation, 0.4, 0.35, 0.3 })
            {
                double d = 1;
                while (d > 0.15 && WorstDeviation(d, f) > allowed) d -= 0.01;
                if (WorstDeviation(d, f) > allowed || !TryApply(d, f)) continue;
                prescale = (float)d;
                Status = string.Format("10% → {0:0}%    25% → {1:0}%    50% → {2:0}%    white → {3:0}%",
                    f(0.1) * 100, f(0.25) * 100, f(0.5) * 100, f(1) * 100);
                return;
            }
            Restore();
            prescale = 1f;
            Status = "Windows refused the curve on this display (HDR may be on).";
        }

        // Games, colour-profile changes or Night Light can reset the gamma curve; put ours back if so.
        public void Verify()
        {
            var current = new ushort[768];
            foreach (Screen sc in Screen.AllScreens)
            {
                ushort[] mine;
                if (!applied.TryGetValue(sc.DeviceName, out mine)) continue;
                IntPtr dc = CreateDC(sc.DeviceName, null, null, IntPtr.Zero);
                if (GetDeviceGammaRamp(dc, current) && (current[255] != mine[255] || current[128] != mine[128] || current[32] != mine[32]))
                    SetDeviceGammaRamp(dc, mine);
                DeleteDC(dc);
            }
        }

        // After a display change the devices may differ.
        public void Reset()
        {
            Restore();
            originals.Clear();
            Off();
        }

        // Windows keeps a gamma curve after the app that set it exits; undo one a crash left behind.
        public void ClearLeftover()
        {
            if (!File.Exists(Marker)) return;
            foreach (Screen sc in Screen.AllScreens)
            {
                IntPtr dc = CreateDC(sc.DeviceName, null, null, IntPtr.Zero);
                SetDeviceGammaRamp(dc, Identity());
                DeleteDC(dc);
            }
            try { File.Delete(Marker); } catch { }
        }

        public void Dispose() { Restore(); }

        bool TryApply(double d, Func<double, double> f)
        {
            bool ok = true;
            foreach (Screen sc in Screen.AllScreens)
            {
                IntPtr dc = CreateDC(sc.DeviceName, null, null, IntPtr.Zero);
                if (dc == IntPtr.Zero) continue;
                ushort[] baseRamp = Original(sc.DeviceName, dc);
                var ramp = new ushort[768];
                for (int c = 0; c < 3; c++)
                    for (int i = 0; i < 256; i++)
                    {
                        // Our curve first, then the display's own calibration ramp.
                        double pos = Lookup(i / 255.0, d, f) * 255;
                        int lo = Math.Max(0, Math.Min(255, (int)pos)), hi = Math.Min(255, lo + 1);
                        double t = pos - lo;
                        ramp[c * 256 + i] = (ushort)Math.Round(baseRamp[c * 256 + lo] * (1 - t) + baseRamp[c * 256 + hi] * t);
                    }
                if (SetDeviceGammaRamp(dc, ramp)) applied[sc.DeviceName] = ramp;
                else ok = false;
                DeleteDC(dc);
            }
            if (ok)
                try
                {
                    Directory.CreateDirectory(Path.GetDirectoryName(Marker));
                    File.WriteAllText(Marker, "");
                }
                catch { }
            return ok;
        }

        ushort[] Original(string device, IntPtr dc)
        {
            ushort[] ramp;
            if (originals.TryGetValue(device, out ramp)) return ramp;
            ramp = new ushort[768];
            if (!GetDeviceGammaRamp(dc, ramp)) ramp = Identity();
            originals[device] = ramp;
            return ramp;
        }

        static ushort[] Identity()
        {
            var ramp = new ushort[768];
            for (int c = 0; c < 3; c++)
                for (int i = 0; i < 256; i++) ramp[c * 256 + i] = (ushort)(i * 257);
            return ramp;
        }

        void Restore()
        {
            foreach (KeyValuePair<string, ushort[]> o in originals)
            {
                if (!applied.ContainsKey(o.Key)) continue;
                IntPtr dc = CreateDC(o.Key, null, null, IntPtr.Zero);
                SetDeviceGammaRamp(dc, o.Value);
                DeleteDC(dc);
            }
            applied.Clear();
            try { File.Delete(Marker); } catch { }
        }
    }
}
