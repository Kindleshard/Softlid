using System;
using System.Collections.Generic;
using System.Drawing;
using System.Globalization;
using System.Threading;
using System.Windows.Forms;
using Microsoft.Win32;
using Timer = System.Windows.Forms.Timer;

namespace BlinkBuddy
{
    // The focus timer, blinks, and look-away and stand-up breaks.
    partial class App
    {
        // ---- The focus timer (work, then rest, again and again) ----

        public const int FocusOff = 0, FocusWorking = 1, FocusPaused = 2, FocusResting = 3;
        public int FocusState { get; private set; }
        public int FocusLeft { get; private set; }
        public int FocusDone { get; private set; }   // work periods finished since BlinkBuddy started

        public void FocusStart()
        {
            if (FocusState == FocusPaused) { FocusState = FocusWorking; return; }
            FocusState = FocusWorking;
            FocusLeft = S.FocusWork * 60;
        }

        public void FocusPause() { if (FocusState == FocusWorking) FocusState = FocusPaused; }
        public void FocusStop() { FocusState = FocusOff; FocusLeft = 0; }

        void FocusTick()
        {
            if (FocusState != FocusWorking || card != null) return;
            if (--FocusLeft > 0) return;
            FocusDone++;
            FocusState = FocusResting;
            card = new LookAwayCard(true, S, S.FocusRest * 60, Lang.T("Time to rest"), Lang.T("Stand up, look away, stretch. The next focus period starts after this."))
            {
                Finished = snooze =>
                {
                    card = null;
                    if (FocusState == FocusResting) { FocusState = FocusWorking; FocusLeft = snooze > 0 ? snooze : S.FocusWork * 60; }
                    TrimMemory();
                }
            };
            card.Show();
        }

        public string FocusText()
        {
            return FocusState == FocusOff ? Clock(S.FocusWork * 60) : FocusState == FocusResting ? Lang.T("Resting") : Clock(FocusLeft);
        }

        // How far along the wait for the next break is (0..1), for the mini icon's rim; -1 when there's none to wait for.
        public float BreakProgress()
        {
            if (Paused) return -1;
            if (FocusState == FocusWorking || FocusState == FocusPaused) return 1f - FocusLeft / (float)Math.Max(1, S.FocusWork * 60);
            if (!S.BreakEnabled) return -1;
            return Math.Min(1f, breakElapsed / (float)Math.Max(1, S.BreakInterval * 60));
        }

        Eyelids lids;

        public void Blink(bool instant)
        {
            if (S.BlinkStyle == 1)
            {
                if (instant) return;   // live tweaks are for the eye icon's size, opacity and place
                if (lids == null) lids = new Eyelids();
                lids.Play(S);
                return;
            }
            if (eye == null) eye = new EyePopup();
            eye.Play(S, scale, instant);
        }

        public void StartBreak(bool standUp)
        {
            if (card != null) return;
            Action<int> finished = snooze =>
            {
                card = null;
                if (standUp)
                {
                    longElapsed = snooze > 0 ? Math.Max(0, S.LongBreakInterval * 60 - snooze) : 0;
                    if (snooze == 0) breakElapsed = 0;
                }
                else breakElapsed = snooze > 0 ? Math.Max(0, S.BreakInterval * 60 - snooze) : 0;
                blinkLeft = S.BlinkInterval;
                TrimMemory();
            };
            // Eye exercises and the stretch coach take the whole screen; otherwise a small card counts down.
            if (!standUp && S.Exercises) card = new GuidedBreak(S) { Finished = finished, Warmth = WarmthNow() };
            else if (standUp && S.StretchGuided) card = new StretchBreak(S) { Finished = finished };
            else card = new LookAwayCard(standUp, S) { Finished = finished };
            card.Show();
        }

        public string BreakStatus()
        {
            if (Paused) return pausedUntil == DateTime.MaxValue ? Lang.T("Reminders paused") : Lang.F("Paused until {0}", pausedUntil.ToShortTimeString());
            if (!S.BreakEnabled) return Lang.T("Breaks are off");
            return Lang.F("Next break in {0}", Clock(S.BreakInterval * 60 - breakElapsed));
        }

        public string LongBreakStatus()
        {
            if (Paused) return Lang.T("Reminders paused");
            if (!S.LongBreakEnabled) return Lang.T("Stand-up reminders are off");
            return Lang.F("Next stand-up in {0}", Clock(S.LongBreakInterval * 60 - longElapsed));
        }
    }
}
