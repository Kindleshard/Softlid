using System;

namespace BlinkBuddy
{
    // Keeps the screen readable whatever Brightness and tone add up to: tone may never leave the screen darker than
    // Brightness alone does at its lowest setting. That is checked at white and at a dark grey (25%), where dark-mode
    // text and panels live. A combination that would go below it gets a higher effective brightness (the slider keeps
    // its value); if even full brightness isn't enough, the tone's curve is eased toward straight. Warmth dims both
    // sides of the comparison alike, so it is left out.
    static class VisibilityGuard
    {
        public const double LowestBrightness = 0.2;   // the Brightness slider's minimum
        const double DarkGrey = 0.25;

        public struct Result
        {
            public double Brightness;   // effective, 0..1
            public double Ease;         // 1 = tone as set, down to 0 = no curve
        }

        // shown(brightness, ease, x): what a grey of x (0..1 as stored) becomes on screen.
        public static Result Solve(double brightness, Func<double, double, double, double> shown)
        {
            Func<double, double, bool> readable = (b, e) =>
                shown(b, e, 1) >= LowestBrightness - 1e-4 && shown(b, e, DarkGrey) >= LowestBrightness * DarkGrey - 1e-4;
            var r = new Result { Brightness = brightness, Ease = 1 };
            for (int step = 0; ; step++)
            {
                double b = Math.Min(1, brightness + step * 0.01);
                if (readable(b, 1))
                {
                    r.Brightness = b;
                    return r;
                }
                if (b >= 1) break;
            }
            r.Brightness = 1;
            for (int step = 1; step <= 20; step++)
            {
                r.Ease = 1 - step * 0.05;
                if (readable(1, r.Ease)) return r;
            }
            r.Ease = 0;
            return r;
        }
    }
}
