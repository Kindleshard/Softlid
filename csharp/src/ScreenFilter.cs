using System;
using System.Collections.Generic;
using System.Drawing;
using System.Windows.Forms;

namespace BlinkBuddy
{
    // Dimming, warm tint and inversion. Prefers the compositor's full-screen colour matrix
    // (no windows, no redraws); falls back to click-through overlay windows if that API refuses.
    class ScreenFilter : IDisposable
    {
        bool magOn, magFailed;
        readonly List<Shade> overlays = new List<Shade>();

        // Whether colour effects can show (the overlay fallback can't do them).
        public bool CanShowEffects { get { return !magFailed; } }

        public string Description
        {
            get
            {
                return magFailed
                    ? "Windows' colour-effect API isn't available here, so a click-through overlay is used instead (invert isn't supported in this mode)."
                    : "Uses Windows' built-in full-screen colour effect: no overlay windows and practically no CPU or GPU cost.";
            }
        }

        // 0% = 6500 K (neutral), 100% = 1900 K (candle-like)
        public static int KelvinFor(double percent) { return (int)Math.Round(Kelvin(percent) / 50.0) * 50; }
        static double Kelvin(double percent) { return 6500 - percent * 46; }

        // night is how far into the night schedule it is (0 = the daytime blue light filter, 1 = the night one); prescale is the tone curve's
        // first stage (see ToneMapper); brightness is the on-screen dimming (1 when the backlight or the curve does it).
        public void Apply(Settings s, float night, float prescale, float brightness)
        {
            bool on = s.FilterEnabled;
            float bright = (on ? brightness : 1f) * prescale;
            bool invert = on && s.Invert;
            int effect = on ? s.ColorEffect : NoEffect;
            // A colour effect and the blue light filter take turns: while an effect is on, its colours aren't warmed (Sepia
            // and Red night already have next to no blue; a warmed Grayscale or E-ink just turns muddy).
            double warm = on ? Night.Blue(s, night) : 0, blue = effect == NoEffect ? warm : 0;
            float r = 1f, g = 1f, b = 1f;
            if (blue != 0) KelvinToRgb(Kelvin(blue), out r, out g, out b);   // below 0: cooler than daylight
            bool neutral = bright >= 1f && r >= 1f && g >= 1f && b >= 1f && !invert && effect == NoEffect;

            if (!magFailed)
            {
                if (neutral) { StopMag(); return; }
                if (!magOn) magOn = Native.MagInitialize();
                Native.MAGCOLOREFFECT e = Effect(r * bright, g * bright, b * bright, invert, effect, s.GrayStyle);
                if (magOn && Native.MagSetFullscreenColorEffect(ref e)) return;
                StopMag();
                magFailed = true;
            }
            // An overlay can't cool, invert or do the effects, so the blue light filter is back on here.
            ApplyOverlays(bright >= 1f && warm <= 0, bright, (float)(Math.Max(0, warm) / 100));
        }

        // Per-channel brightness and warmth multipliers, for the smart filter to apply in its shader instead.
        public static void Scale(Settings s, float night, out float r, out float g, out float b)
        {
            Scale(s, night, s.Brightness / 100f, out r, out g, out b);
        }

        // Same, with the on-screen brightness given (the visibility guard may raise it above the setting).
        public static void Scale(Settings s, float night, float bright, out float r, out float g, out float b)
        {
            r = g = b = 1f;
            if (!s.FilterEnabled) return;
            double blue = Night.Blue(s, night);
            if (blue != 0) KelvinToRgb(Kelvin(blue), out r, out g, out b);
            r *= bright;
            g *= bright;
            b *= bright;
        }

        // Used when the smart filter takes over, and after unlock or a display change when the effect may be stale.
        public void Off()
        {
            StopMag();
            CloseOverlays();
        }

        public static bool SelfTest()
        {
            if (!Native.MagInitialize()) return false;
            Native.MAGCOLOREFFECT e = Effect(1f, 1f, 1f, false);
            bool ok = Native.MagSetFullscreenColorEffect(ref e);
            Native.MagUninitialize();
            return ok;
        }

        public void Dispose()
        {
            StopMag();
            CloseOverlays();
        }

        // Row-vector matrix: out = [r g b a 1] x M, so row 4 holds the offsets (for inversion and the effects).
        static Native.MAGCOLOREFFECT Effect(float r, float g, float b, bool invert) { return Effect(r, g, b, invert, 0, 0); }

        // The colour effect first (grayscale, sepia, e-ink, red night: a 3x3 mix plus offsets), then the per-channel
        // brightness and warmth k, then inversion: out_j = k_j * (mix_j + c_j), or k_j * (1 - mix_j - c_j) inverted.
        static Native.MAGCOLOREFFECT Effect(float r, float g, float b, bool invert, int effect, int style)
        {
            var e = new Native.MAGCOLOREFFECT { transform = new float[25] };
            float[] k = { r, g, b };
            float[,] m;
            float[] c;
            Mix(effect, style, out m, out c);
            for (int j = 0; j < 3; j++)
            {
                for (int i = 0; i < 3; i++) e.transform[i * 5 + j] = (invert ? -m[i, j] : m[i, j]) * k[j];
                e.transform[20 + j] = (invert ? 1 - c[j] : c[j]) * k[j];
            }
            e.transform[18] = 1f;
            e.transform[24] = 1f;
            return e;
        }

        // The colour effects: 0 none, 1 grayscale (style 0 Rec.709, 1 Rec.601, 2 black and white), 2 sepia,
        // 3 e-ink (soft grey on warm paper), 4 red night (everything in shades of red, for the dark).
        public const int NoEffect = 0, Grayscale = 1, Sepia = 2, Eink = 3, RedNight = 4;
        static void Mix(int effect, int style, out float[,] m, out float[] c)
        {
            m = new float[3, 3];
            c = new float[3];
            float[] w = style == 0 ? new[] { 0.2126f, 0.7152f, 0.0722f } : new[] { 0.299f, 0.587f, 0.114f };
            switch (effect)
            {
                case Grayscale:
                    float gain = style == 2 ? 1.4f : 1f, lift = style == 2 ? -0.2f : 0f;   // black and white: more contrast, but greys above ~15% stay visible
                    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) m[i, j] = w[i] * gain;
                    c[0] = c[1] = c[2] = lift;
                    break;
                case Sepia:
                    float[,] s = { { 0.393f, 0.349f, 0.272f }, { 0.769f, 0.686f, 0.534f }, { 0.189f, 0.168f, 0.131f } };
                    m = s;
                    break;
                case Eink:
                    float[] paper = { 1f, 0.965f, 0.89f };
                    w = new[] { 0.299f, 0.587f, 0.114f };
                    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) m[i, j] = w[i] * 0.84f * paper[j];
                    for (int j = 0; j < 3; j++) c[j] = 0.08f * paper[j];
                    break;
                case RedNight:
                    for (int i = 0; i < 3; i++) m[i, 0] = w[i];
                    break;
                default:
                    m[0, 0] = m[1, 1] = m[2, 2] = 1f;
                    break;
            }
        }

        // What a colour looks like through an effect (for the previews in the settings).
        public static Color Through(Color x, int effect, int style)
        {
            float[,] m;
            float[] c;
            Mix(effect, style, out m, out c);
            float[] i = { x.R / 255f, x.G / 255f, x.B / 255f }, o = new float[3];
            for (int j = 0; j < 3; j++) o[j] = Math.Max(0f, Math.Min(1f, i[0] * m[0, j] + i[1] * m[1, j] + i[2] * m[2, j] + c[j]));
            return Color.FromArgb((int)(o[0] * 255), (int)(o[1] * 255), (int)(o[2] * 255));
        }

        void StopMag()
        {
            if (!magOn) return;
            Native.MAGCOLOREFFECT identity = Effect(1f, 1f, 1f, false);
            Native.MagSetFullscreenColorEffect(ref identity);
            Native.MagUninitialize();
            magOn = false;
        }

        // Tanner Helland's black-body approximation, normalised so 6500 K is exactly neutral.
        static void KelvinToRgb(double kelvin, out float r, out float g, out float b)
        {
            double r0, g0, b0, r1, g1, b1;
            Helland(6500, out r0, out g0, out b0);
            Helland(kelvin, out r1, out g1, out b1);
            r = (float)Math.Min(1, r1 / r0);
            g = (float)Math.Min(1, g1 / g0);
            b = (float)Math.Min(1, b1 / b0);
        }

        static void Helland(double kelvin, out double r, out double g, out double b)
        {
            double t = kelvin / 100;
            r = t <= 66 ? 255 : 329.698727446 * Math.Pow(t - 60, -0.1332047592);
            g = t <= 66 ? 99.4708025861 * Math.Log(t) - 161.1195681661 : 288.1221695283 * Math.Pow(t - 60, -0.0755148492);
            b = t >= 66 ? 255 : t <= 19 ? 0 : 138.5177312231 * Math.Log(t - 10) - 305.0447927307;
            r = Math.Max(0, Math.Min(255, r));
            g = Math.Max(0, Math.Min(255, g));
            b = Math.Max(0, Math.Min(255, b));
        }

        // An overlay can only blend, not multiply: dim with black, then tint with amber, folded into one colour + alpha.
        void ApplyOverlays(bool neutral, float bright, float warmth)
        {
            if (neutral) { CloseOverlays(); return; }
            Screen[] screens = Screen.AllScreens;
            if (overlays.Count != screens.Length)
            {
                CloseOverlays();
                foreach (Screen sc in screens) overlays.Add(new Shade(sc.Bounds, Color.Black, 0.01));
            }
            float d = 1f - bright, t = warmth * 0.45f;
            float a = 1f - (1f - d) * (1f - t);
            float amber = a > 0.001f ? (1f - d) * t / a : 0f;
            Color c = Color.FromArgb((int)(255 * amber), (int)(120 * amber), 0);
            foreach (Shade o in overlays)
            {
                o.BackColor = c;
                o.Opacity = Math.Max(0.01, a);
                if (!o.Visible) o.Show();
            }
        }

        void CloseOverlays()
        {
            foreach (Shade o in overlays) o.Close();
            overlays.Clear();
        }
    }
}
