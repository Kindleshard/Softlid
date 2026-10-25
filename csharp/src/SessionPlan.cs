using System;

namespace BlinkBuddy
{
    // When each step of a guided break starts, so every spoken line plays to its end: a step lasts at least its line
    // plus a short pause, spare time is shared out evenly, and the closing line fits in the time too. Without voice,
    // the time is simply split evenly (but no step is shorter than minStep).
    class SessionPlan
    {
        public readonly double[] Start, Length;
        public readonly double End;   // when the last step finishes and the closing line (if any) begins
        public readonly double Lead;  // quiet at the very start, for the opening chime to ring out before the first line

        // weight: how big a share of the spare time each step gets (null: equal shares).
        public SessionPlan(string[] keys, string doneKey, double total, double minStep, double lead, bool voiced, double[] weight = null)
        {
            int n = keys.Length;
            Start = new double[n];
            Length = new double[n];
            double sum = 0;
            for (int i = 0; i < n; i++) sum += weight == null ? 1 : weight[i];
            if (voiced)
            {
                double spoken = Voice.Length(doneKey);
                for (int i = 0; i < n; i++) spoken += Voice.Length(keys[i]) + Voice.Pause;
                double spare = Math.Max(0, total - lead - spoken);
                for (int i = 0; i < n; i++) Length[i] = Voice.Length(keys[i]) + Voice.Pause + spare * (weight == null ? 1 : weight[i]) / sum;
                Length[0] += lead;
                Lead = lead;
            }
            else
                for (int i = 0; i < n; i++) Length[i] = Math.Max(minStep, total * (weight == null ? 1 : weight[i]) / sum);
            double t = 0;
            for (int i = 0; i < n; i++)
            {
                Start[i] = t;
                t += Length[i];
            }
            End = t;
        }

        // Which step is running at time t (-1 before the first), and how far into it.
        public int At(double t, out double into)
        {
            into = 0;
            if (t < 0) return -1;
            for (int i = Start.Length - 1; i >= 0; i--)
                if (t >= Start[i])
                {
                    into = t - Start[i];
                    return i;
                }
            return 0;
        }
    }
}
