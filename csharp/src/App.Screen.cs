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
    // The screen: the night schedule and its place, the filter pipeline (tone engines, the test lab, DesktopLUT), screen modes and per-app rules, and the real backlight.
    partial class App
    {
        // How far into the night schedule it is: 0 by day (or with no schedule), 1 at night.
        // The night strength the screen shows: none while an app's mode is in use (that mode is shown as it is, e.g.
        // Editing keeps true colour at night too).
        float NightShown() { return ruleMode >= 0 ? 0f : NightNow(); }

        float NightNow()
        {
            if (DateTime.Now < previewNightUntil) return 1f;   // a moment's preview of the night strength
            if (!S.ScheduleEnabled) return 0f;
            if (DateTime.Now < previewUntil) return 0f;
            return Night.Factor(S, DateTime.Now);
        }

        // While a blue light slider is being moved, show what it sets, whatever the time of day: the daytime one for a
        // few seconds; the schedule's night one only for a moment, then back to the daytime one until night comes.
        public void PreviewNight() { previewUntil = DateTime.Now.AddSeconds(5); previewNightUntil = DateTime.MinValue; }
        public void PreviewNightStrength() { previewNightUntil = DateTime.Now.AddSeconds(1.2); }
        DateTime previewNightUntil = DateTime.MinValue;

        // The blue light filter in effect right now (0 when the filter is off), for things drawn to match it.
        // (None while a colour effect is on: it takes the blue light filter's place, and the screen shows things through it.)
        public float WarmthNow() { Settings v = screenNow ?? S; return v.FilterEnabled && !effectShown ? (float)(Night.Blue(v, NightShown()) / 100) : 0f; }
        bool effectShown;

        // Whether colour effects can show now: not while the Smart (GPU) engine does the work, nor with the overlays.
        public bool EffectsAvailable { get { return Filter.CanShowEffects && !(S.ToneEnabled && S.ToneMode == 2 && !smart.Failed); } }

        void ApplyScreen()
        {
            // The screen shows your settings, or (while an app with a rule is in front) a copy with its mode applied.
            Settings S = screenNow = Effective();
            if (stripKeysOn != (S.StripEnabled && !S.StripFollow)) RegisterHotkeys();
            bool on = S.FilterEnabled;
            // Tone has its own switch; the screen filter switch turns everything off, tone included. The test lab's
            // compare button shows the screen without tone (DesktopLUT gets a do-nothing table instead of stopping).
            int engine = S.ToneMode;
            bool toneOn = on && S.ToneEnabled && !(labCompare && engine != 4);
            // Smart (GPU): the GPU filter does tone, brightness, warmth and invert itself.
            bool useSmart = toneOn && engine == 2 && !smart.Failed;
            // A colour effect pauses the blue light filter, where one can show (not in the GPU filter or the overlays).
            effectShown = on && S.ColorEffect != ScreenFilter.NoEffect && !useSmart && Filter.CanShowEffects;
            // Smart layer: a dimming layer does the tone; the colour effect still does brightness, warmth and invert.
            bool useLayer = toneOn && engine == 3 && !smartLayer.Failed;
            // DesktopLUT: it does the tone inside the compositor; the colour effect still does brightness, warmth, invert.
            // A strength change writes that strength's LUT and restarts DesktopLUT with it.
            if (toneOn && engine == 4)
            {
                bool written = WriteLut();
                if (lut.Running) { if (written) lut.Reload(); }
                else lut.Ensure(S, false);   // may fail at once (not found, permission declined)
            }
            bool useLut = toneOn && engine == 4 && !lut.Failed;
            if (!useLut) lut.Stop();
            // Standard: its presets (with the adapt-to-content dimming), or custom sliders. An experimental engine that
            // fails falls back to the standard preset at the same strength.
            bool custom = toneOn && S.CustomTone && (S.ShadowCrush > 0 || S.Squeeze > 0 || S.LimiterStrength > 0);
            bool auto = toneOn && !S.CustomTone && !useSmart && !useLayer && !useLut;
            comfort.Apply(S, auto);

            // What a grey becomes on screen for a brightness and an easing of the curve, per engine, for the guard.
            // (The backlight is separate: its own slider, outside this; Brightness always dims pixels.)
            double gamma = Comfort.Gamma(S), white = comfort.White;
            int lvl = S.Level;
            bool lutPlain = labCompare;   // DesktopLUT showing its do-nothing table
            Func<double, double, double, double> shown;
            if (auto) shown = (b, e, x) => b * white * Math.Pow(x, 1 + (gamma - 1) * e);
            else if (custom) shown = (b, e, x) => b * ToneMapper.Net(x, (int)(S.ShadowCrush * e), (int)(S.Squeeze * e), (int)(S.LimiterStrength * e));
            else if (useSmart || useLayer) shown = (b, e, x) => b * SmartFilter.Flat(lvl, x);
            else if (useLut && !lutPlain) shown = (b, e, x) => SmartFilter.Flat(lvl, b * x);   // the LUT comes after on-screen dimming
            else shown = (b, e, x) => b * x;
            VisibilityGuard.Result guard = on ? VisibilityGuard.Solve(S.Brightness / 100.0, shown)
                                              : new VisibilityGuard.Result { Brightness = 1, Ease = 1 };
            GuardNote = guard.Ease < 1 ? Lang.T("Visibility guard: brightness at 100% and the curve eased so dark details stay visible.")
                : guard.Brightness > S.Brightness / 100.0 + 0.005
                    ? Lang.F("Visibility guard: brightness held at {0:0}% (set to {1}%) so the screen stays readable.", guard.Brightness * 100, S.Brightness)
                    : "";
            float bright = (float)guard.Brightness;   // on-screen dimming

            // Standard and Custom: brightness belongs after the curve. The colour effect runs before the gamma curve, so
            // it is fed brightness^(1/gamma) for the presets, and the custom curve carries brightness itself; otherwise
            // the curve would multiply the dimming (20% brightness at Max would leave white at 3%).
            double g = 1 + (gamma - 1) * guard.Ease;
            int sh = (int)(S.ShadowCrush * guard.Ease), sq = (int)(S.Squeeze * guard.Ease), li = (int)(S.LimiterStrength * guard.Ease);
            string key = auto ? "auto" + g.ToString("0.000")
                : custom ? string.Format("{0},{1},{2}@{3:0.00}", sh, sq, li, bright)
                : "off";
            if (key != toneKey)
            {
                toneKey = key;
                if (auto) tone.SetCurve(x => Math.Pow(x, g));
                else if (custom) tone.SetCurve(x => bright * ToneMapper.Net(x, sh, sq, li));
                else tone.Off();
            }
            bool curveRefused = tone.Status.StartsWith("Windows refused");
            float prescale = tone.Prescale, onScreen = bright;
            if (auto) { prescale *= (float)Math.Pow(bright * white, 1 / g); onScreen = 1f; }
            else if (custom && !curveRefused) onScreen = 1f;
            night = NightShown();
            if (useSmart)
            {
                Filter.Off();
                smart.Apply(SmartParams(bright), ui);
                colourGain = 1f;
            }
            else
            {
                smart.Stop();
                Filter.Apply(S, night, prescale, onScreen);
                colourGain = on ? prescale * onScreen : 1f;
            }
            // Static grain rides in the smart layer, so there is one layer over the screen instead of two.
            bool bakeGrain = useLayer && S.GrainEnabled && !S.GrainAnimated;
            if (useLayer)
            {
                var lp = new SmartLayer.Params
                {
                    Level = S.Level, Gain = colourGain, Grain = bakeGrain,
                    GrainAlpha = (int)Math.Round(S.GrainStrength * 1.28), GrainTone = S.GrainTone,
                    GrainBlock = S.GrainCoarse ? Math.Max(2, (int)Math.Round(2 * scale)) : 1
                };
                smartLayer.Apply(lp, ui);
            }
            else smartLayer.Stop();
            int backlight = S.HardwareBrightness ? S.Backlight : -1;
            if (backlight >= 0 && backlight != lastBacklight)
            {
                lastBacklight = backlight;
                Backlight.Set(backlight);
            }
            strip.Apply(S, scale);
            grain.Apply(S, scale, bakeGrain);
        }

        public string LayerStatus { get { return smartLayer.Status; } }
        public string GuardNote = "";   // set while the visibility guard is holding brightness up or easing the curve

        // The tone test lab changes engine, strength and brightness freely; everything comes back when it closes.
        Settings labSaved;
        bool labCompare;
        ToneLab lab;

        public void OpenToneLab()
        {
            if (lab != null && !lab.IsDisposed)
            {
                lab.Activate();
                return;
            }
            // The lab takes the settings window's place, which reopens afresh when the lab closes.
            bool reopen = settings != null && !settings.IsDisposed;
            if (reopen) settings.Close();
            LabBegin();
            lab = new ToneLab(this);
            lab.FormClosed += delegate
            {
                lab = null;
                LabEnd();
                if (reopen) ShowSettings();
            };
            lab.Show();
        }

        public void LabBegin()
        {
            labSaved = new Settings();
            labSaved.CopyFrom(S);
            S.FilterEnabled = S.ToneEnabled = true;
            if (S.CustomTone) S.ComfortLevel = 1;
            Changed(true);
        }

        // Hold to see the screen without tone (DesktopLUT keeps running, with a table that changes nothing).
        public void LabCompare(bool without)
        {
            labCompare = without;
            ApplyScreen();
        }

        public void LabEnd()
        {
            labCompare = false;
            if (labSaved == null) return;
            string confirmed = S.LutConfirmedBuild;   // a DesktopLUT check passed in the lab still counts
            S.CopyFrom(labSaved);
            S.LutConfirmedBuild = confirmed;
            labSaved = null;
            SetToneMode(S.ToneMode);   // clears engine failures from the lab and reapplies
        }

        // Writes the LUT for the current strength (or the do-nothing one the lab compares against) if it isn't the one
        // DesktopLUT was last given.
        bool WriteLut()
        {
            int level = labCompare ? -1 : S.Level;
            string key = level < 0 ? "plain" : Comfort.Levels[level];
            if (key == lutKey && System.IO.File.Exists(LutGuard.CubePath)) return false;
            try { LutWriter.WriteLevel(LutGuard.CubePath, level); }
            catch { return false; }
            lutKey = key;
            return true;
        }

        public string LutStatus { get { return lut.Status; } }
        public bool LutFailed { get { return lut.Failed; } }

        // Re-run DesktopLUT's 15-second check (after changing its LUT, for example).
        public void RetestLut()
        {
            lut.Stop();
            lut.ClearFailure();
            dirty = true;
            if (S.ToneMode == 4)
            {
                S.FilterEnabled = S.ToneEnabled = true;   // the check is meaningless with tone off
                WriteLut();
                lut.Ensure(S, true);
            }
            ApplyScreen();
        }

        public double ComfortNet(double x) { return comfort.Net(x); }

        public string ComfortStatus()
        {
            if (tone.Status.StartsWith("Windows refused"))
                return Lang.T("Windows won't accept a gamma curve here (HDR may be on), so only the even dimming is active.");
            return Lang.F(comfort.Adapting
                    ? "Whites at {0:0}% right now, a little lower because the screen is mostly bright.\nDark tones go deeper; bright shades spread apart, so text and colours stay clear."
                    : "Whites at {0:0}% right now.\nDark tones go deeper; bright shades spread apart, so text and colours stay clear.",
                comfort.White * 100);
        }

        SmartFilter.Params SmartParams(float bright)
        {
            SmartFilter.Params p = SmartFilter.ForLevel(S.Level);
            ScreenFilter.Scale(screenNow ?? S, night, bright, out p.R, out p.G, out p.B);   // (a rule's mode, if one applies)
            p.Invert = S.Invert;
            return p;
        }

        public string ToneStatus { get { return tone.Status; } }
        public string SmartStatus { get { return smart.Status; } }

        // The compact view keeps only the everyday features: the advanced ones are switched off, not just hidden.
        public void UseCompactView()
        {
            S.AdvancedView = false;
            S.Invert = false;
            S.CoolRange = false;   // cooler than daylight is an advanced setting too
            S.ScheduleMode = 0;    // and so are custom night hours: Simple follows sunset and sunrise
            S.BlueFilter = Math.Max(0, S.BlueFilter);
            S.KeptBlue = Math.Max(0, S.KeptBlue);
            S.GrainEnabled = false;
            if (S.CustomTone) S.ComfortLevel = 1;
            else if (S.ComfortLevel == 3) S.ComfortLevel = 2;   // Max: the strongest compact step
            // What lives under "More options" (which Simple doesn't show) and would otherwise go on working unseen:
            S.GrayStyle = 0;
            S.NightFade = 60;
            S.ForceRest = false;
            S.StripFollow = true;   // (moving it with the keys, or dimming one side only, would be a mystery)
            S.StripCover = 0;
            if (S.KeepAwake || KeepingAwake) SetKeepAwake(false);
            // (Modes for apps stay listed, but only apply in the Advanced view: see CheckRules.)
            if (S.ToneMode != 0) SetToneMode(0);   // the engines are Advanced too: Standard takes over
            else Changed(true);
        }

        public void SetToneMode(int mode)
        {
            S.ToneMode = mode;
            if (mode != 0 && S.ComfortLevel == Settings.CustomLevel) S.ComfortLevel = 1;   // custom is standard-only
            smart.ClearFailure();
            smartLayer.ClearFailure();
            lut.ClearFailure();
            Changed(true);
        }

        // ---- Screen modes and per-app rules ----

        int ruleMode = -1;        // the mode a rule applies right now (-1: none)
        Settings screenNow;       // what the screen shows: the settings, or a copy with a rule's mode applied

        public void ChooseMode(int mode)
        {
            Modes.Choose(S, mode);
            Changed(true);
            if (settings != null) settings.SyncFromApp();
        }

        // A slider or a hotkey moved: the screen is your own setting again, not a preset.
        public void ManualScreenChange()
        {
            if (S.Mode != Modes.Custom) S.Mode = Modes.Custom;
        }

        public int RuleMode { get { return ruleMode; } }

        Settings Effective()
        {
            if (ruleMode < 0) return S;
            var v = new Settings();
            v.CopyFrom(S);
            Modes.Apply(v, ruleMode);
            return v;
        }

        // Is an app with a rule in front? Checked the moment another window comes to the front (and each second, to be sure).
        // A window that only passes through - the taskbar, Start, Alt+Tab, BlinkBuddy itself - leaves the app's mode on
        // while the app's window is still there to see. (Except this settings window: in it you see and set your own values.)
        IntPtr ruleWindow;
        Modes.FrontWatch frontWatch;

        void CheckRules()
        {
            if (S.AppRules.Length == 0 && ruleMode < 0) return;
            int m;
            string front;
            IntPtr window;
            if (!S.AdvancedView) m = -1;   // (an Advanced feature: kept, but idle in Simple)
            else if (settings != null && settings.ContainsFocus) m = -1;   // your own values while you set them
            else if (Modes.ForegroundApp(out front, out window))
            {
                m = Modes.RuleFor(S, front);
                ruleWindow = m >= 0 ? window : IntPtr.Zero;
            }
            else m = ruleMode >= 0 && Modes.StillShown(ruleWindow) ? ruleMode : -1;
            if (m == ruleMode) return;
            ruleMode = m;
            ApplyScreen();
        }

        public string NightStatus() { return Night.Status(S, DateTime.Now); }

        // Sunset to sunrise needs to know where the PC is: ask Windows location now and then (a few times a day is
        // plenty), keeping the last answer when location is off.
        DateTime locatedAt = DateTime.MinValue;
        public DateTime SyncedAt = DateTime.MinValue;   // when the sun times were last tied to a place

        // The line under the schedule's status: 0 still syncing, 1 synced (with Windows location or the time zone's
        // city), 2 failed (neither works: a city has to be chosen), 3 a city you chose.
        public const int Syncing = 0, Synced = 1, SyncFailed = 2, CityChosen = 3;
        public int SyncState(out string text)
        {
            bool windows;
            string place = Night.Place(S, out windows);
            if (S.CityManual && place != null)
            {
                text = Lang.F("{0} time, chosen by you", place);
                return CityChosen;
            }
            if (SyncedAt == DateTime.MinValue && !S.CityManual)
            {
                text = Lang.T("Syncing with your location...");
                return Syncing;
            }
            if (windows) { text = Lang.T("Synced with your Windows location"); return Synced; }
            if (place != null) { text = Lang.F("Synced with {0} time", place); return Synced; }
            text = Lang.T("Couldn't sync.");
            return SyncFailed;
        }

        // A city chosen by hand ("" goes back to syncing by itself: Windows location, else the time zone).
        public void SetCity(string city)
        {
            S.CityManual = city.Length > 0;
            S.City = city;
            SyncedAt = locatedAt = DateTime.MinValue;   // automatic again: look it up on the next tick
            dirty = true;
            ApplyScreen();
        }

        void UpdateLocation()
        {
            if (!S.ScheduleEnabled || S.ScheduleMode != 0 || S.CityManual || (DateTime.Now - locatedAt).TotalHours < 6) return;
            locatedAt = DateTime.Now;
            Night.Locate((lat, lon) => ui.Post(delegate
            {
                SyncedAt = DateTime.Now;   // whichever answered: Windows location, or (without it) the time zone
                if (double.IsNaN(lat)) return;
                string where = Night.Coordinates(lat, lon);
                if (where == S.City) return;
                S.City = where;
                dirty = true;
                ApplyScreen();
            }, null));
        }

        // Switching on first asks the monitors (on a pool thread) whether they answer over DDC/CI.
        public void SetHardwareBrightness(bool on, Action done)
        {
            int id = ++probeId;
            if (!on)
            {
                if (S.HardwareBrightness && S.HardwareRestore >= 0) Backlight.Set(S.HardwareRestore);
                S.HardwareBrightness = false;
                lastBacklight = -1;
                BacklightStatus = "";
                Changed(true);
                done();
                return;
            }
            BacklightStatus = "Checking your monitors...";
            Backlight.Probe((supported, total, current) => ui.Post(delegate
            {
                if (id != probeId) return;   // switched off again while checking
                if (supported == 0)
                    BacklightStatus = "No display answered. On external monitors, DDC/CI may be off in the monitor's menu.";
                else
                {
                    S.HardwareBrightness = true;
                    S.HardwareRestore = current;
                    S.Backlight = current;
                    lastBacklight = current;
                    BacklightStatus = BacklightText(supported, total);
                }
                Changed(true);
                done();
            }, null));
        }

        void RefreshBacklightStatus()
        {
            Backlight.Probe((supported, total, current) => ui.Post(delegate
            {
                BacklightStatus = supported == 0 ? "No display answered just now." : BacklightText(supported, total);
            }, null));
        }

        static string BacklightText(int supported, int total)
        {
            return supported == total
                ? (total == 1 ? Lang.T("Controlling the backlight of 1 display.") : Lang.F("Controlling the backlight of {0} displays.", total))
                : Lang.F("Controlling the backlight of {0} of {1} displays.", supported, total);
        }
    }
}
