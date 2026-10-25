using System;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Text;

namespace BlinkBuddy
{
    // Every public field is persisted as name=value in %AppData%\BlinkBuddy\settings.ini.
    class Settings
    {
        // Screen
        public bool FilterEnabled = true;
        public int Brightness = 100;      // %, 20..100
        public int BlueFilter = 0;        // %, 0..100 maps 6500 K -> 1900 K
        public bool Invert = false;
        public int ColorEffect = 0;              // 0 none, 1 grayscale, 2 sepia, 3 e-ink, 4 red night (ScreenFilter)
        public int GrayStyle = 0;                // grayscale as 0 Rec.709, 1 Rec.601, 2 black and white
        public bool CoolRange = false;           // the blue light slider also goes cooler than daylight (to about 10000 K)
        public int Mode = -1;                    // the screen mode picked (Modes; -1 none yet, 7 Custom)
        public bool KeptFilter = true, KeptTone = true;   // your own values, kept while another mode is in use
        public int KeptBrightness = 100, KeptBlue = 0, KeptEffect = 0;
        public string AppRules = "";             // per-app modes: app.exe=mode=path, separated by bars (the path is for its name and icon)
        public bool KeepAwake = false;           // keep the PC from sleeping and the screen from turning off
        public string Hotkeys = "";              // custom shortcuts, action=keys separated by semicolons (empty: defaults)
        public bool ForceRest = false;           // breaks can't be skipped (no Skip, Snooze or Esc)
        public bool MiniCountdown = true;        // the mini icon's rim shows the time to the next break
        public int FocusWork = 25, FocusRest = 5;   // min: the focus timer's work and rest periods
        public bool HardwareBrightness = false;  // also drive the display's real backlight (DDC/CI or the laptop panel)
        public int Backlight = -1;               // %, that backlight level; separate from Brightness, which dims pixels
        public int HardwareRestore = -1;         // backlight % to return to when that is switched off

        // Tone
        public bool ToneEnabled = true;
        public int ToneMode = 0;                 // engine: 0 standard, 2 smart GPU filter, 3 smart layer, 4 DesktopLUT (1 was manual)
        public string LutPath = @"C:\Tools\DesktopLUT\DesktopLUT.exe";
        public string LutConfirmedBuild = "";    // Windows build the DesktopLUT trial was last confirmed on
        public int ComfortLevel = 1;             // strength: 0 gentle, 1 balanced, 2 strong, 3 max, 4 custom (standard only)
        public bool AdaptToContent = true;
        public int ShadowCrush = 0;              // custom, %: darker darks, pulled together toward black
        public int Squeeze = 0;                  // custom, %: highlights compressed toward the darks
        public int LimiterStrength = 0;          // custom, %: 100 caps the brightest whites at 20%

        public const int CustomLevel = 4;
        // The preset strength the engines use (custom only exists for standard).
        public int Level { get { return Math.Min(ComfortLevel, CustomLevel - 1); } }
        public bool CustomTone { get { return ToneMode == 0 && ComfortLevel == CustomLevel; } }

        // Night schedule for the blue light filter
        public bool ScheduleEnabled = false;
        public int ScheduleMode = 0;             // 0 sunset to sunrise, 1 custom hours
        public string City = "";                 // "lat, lon" last given by Windows location; empty = guess from the time zone
        public bool CityManual = false;          // City is a city you picked (then Windows location doesn't replace it)
        public int NightFade = 60;               // min: the filter eases in after the start and out before the end
        public int NightFrom = 21 * 60;          // minutes after midnight
        public int NightTo = 7 * 60;
        public int NightBlue = 60;               // %: the blue light filter the schedule moves to at night (overrides the daytime value)

        // Reading strip
        public bool StripEnabled = false;
        public int StripHeight = 120;            // px at 100% display scaling
        public int StripDim = 70;                // %
        public int StripFeather = 60;            // px at 100% scaling: soft edge width, 0 = sharp
        public int StripShape = 0;               // 0 horizontal band, 1 vertical band, 2 rectangle, 3 circle
        public int StripWidth = 640;             // px at 100%: the width of a vertical band or a rectangle
        public int StripCover = 0;               // bands: 0 dim both sides, 1 only above/left, 2 only below/right
        public int StripColor = 0;               // the dim's tint (ARGB; a small number is an old index into Theme.StripColors)
        public int StripEdge = 1;                // the clear area's edge: 0 sharp, 1 soft (StripFeather), 2 an outline (StripBorder)
        public int StripBorder = 2;              // px: the outline's width
        public int StripLine = 0;                // the outline's colour (ARGB; 0 = the accent)
        public bool StripFollow = true;          // follows the mouse (off: stays put and moves with the keys)
        public bool StripOneMonitor = false;     // stays on the monitor it is on
        public int StripAccel = 50;              // how much held movement keys speed up (0 = steady)

        // Film grain
        public bool GrainEnabled = false;
        public int GrainStrength = 25;           // %, 100 = specks up to half-opaque
        public bool GrainCoarse = false;
        public int GrainTone = 1;                // 0 darker specks only, 1 mixed, 2 lighter specks only
        public bool GrainAnimated = false;

        // Blink reminder
        public bool BlinkEnabled = true;
        public int BlinkInterval = 15;    // s
        public int IconSize = 96;         // px at 100% display scaling
        public int IconOpacity = 70;      // %
        public int IconCorner = 1;        // 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right, 4 centre
        public int BlinkStyle = 0;        // 0 the eye icon, 1 eyelids closing over the whole screen
        public bool BlinkFade = false;    // dim the whole display in step with the icon's blink (an on-screen shade)
        public int BlinkFadeDepth = 100;   // %, how dark the screen gets at the bottom of that blink

        // Look-away breaks (20-20-20 rule)
        public bool BreakEnabled = true;
        public int BreakInterval = 20;    // min
        public int BreakDuration = 20;    // s
        public bool Sounds = true;
        public bool Exercises = true;            // guided eye movements during look-away breaks
        public bool ExerciseVoice = false;       // read the exercise steps aloud
        public bool StrictBreaks = false;        // dim every screen while a break runs
        public bool BreakBackdrop = true;        // a dark background behind breaks (off: the eye exercises play over the screen)

        // Stand-up breaks
        public bool LongBreakEnabled = false;
        public int LongBreakInterval = 60;       // min
        public int LongBreakDuration = 5;        // min
        public bool StretchGuided = true;        // a full-screen coach shows stretches during stand-up breaks
        public bool StretchVoice = false;        // and announces each one aloud

        // General
        public bool PauseInFullscreen = true;
        public bool AdvancedView = false;        // show the settings most people don't need (invert, grain, experimental tone)
        public bool MiniMode = false;            // the settings window is folded into the small always-on-top eye
        public int MiniX = -1, MiniY = -1;       // where that eye sits; -1 = top-right of the main screen
        public int MiniFadeDelay = 500;          // ms after the pointer leaves the eye before it fades back
        public int MiniFadeTime = 150;           // ms the fade itself takes (0 = at once)
        public bool MiniEnabled = true;          // mini mode is available (the button by the title bar)
        public int MiniIcon = 0;                 // 0 eye, 1 lamp, 2 sun, 3 laptop
        public bool MiniOrbit = false;           // now and then a soft light runs once round the edge of the mini icon
        public int MiniOrbitSeconds = 60;        // how often, in seconds (0: one round after another)
        public bool AccentFromWindows = false;   // the accent colour follows Windows'
        public int AccentColor = Theme.AccentColors[0].ToArgb();   // otherwise this colour (ARGB; a small number is an old index into Theme.AccentColors)
        public int MiniOrbitColor = 0;           // the glow's colour (ARGB; 0 = the accent, 1..5 an old index into Theme.OrbitColors)
        public int CloseAction = 0;              // closing the settings window: 0 ask, 1 keep running in the tray, 2 exit
        public string Folded = "";               // settings sections folded shut (their titles, separated by bars)
        public int SettingsHeight = 0;           // the settings window's height at 100% scaling (0 = the standard one)
        public string Language = "";             // interface language code; empty = the Windows language (if translated)
        public bool UpdateCheck = true;          // look for a newer BlinkBuddy once a week
        public string LastUpdateCheck = "";      // when it last looked (yyyy-MM-dd)

        static string FilePath
        {
            get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "BlinkBuddy", "settings.ini"); }
        }

        static FieldInfo[] Fields { get { return typeof(Settings).GetFields(BindingFlags.Public | BindingFlags.Instance); } }

        public static Settings Load()
        {
            var s = new Settings();
            bool nightSaved = false;
            bool edgeSaved = false;
            try
            {
                if (!File.Exists(FilePath)) return s;
                foreach (string line in File.ReadAllLines(FilePath))
                {
                    int eq = line.IndexOf('=');
                    if (eq <= 0) continue;
                    FieldInfo f = typeof(Settings).GetField(line.Substring(0, eq).Trim());
                    if (f == null) continue;
                    string v = line.Substring(eq + 1).Trim();
                    bool b; int i;
                    if (f.FieldType == typeof(bool) && bool.TryParse(v, out b)) f.SetValue(s, b);
                    else if (f.FieldType == typeof(int) && int.TryParse(v, NumberStyles.Integer, CultureInfo.InvariantCulture, out i)) f.SetValue(s, i);
                    else if (f.FieldType == typeof(string)) f.SetValue(s, v);
                    if (f.Name == "NightBlue") nightSaved = true;
                    if (f.Name == "StripEdge") edgeSaved = true;
                }
                // Before the schedule had a strength of its own it brought the blue light filter in only at night;
                // keep it that way: the old value becomes the night one, and days stay neutral.
                if (!nightSaved && s.ScheduleEnabled)
                {
                    s.NightBlue = s.BlueFilter;
                    s.BlueFilter = 0;
                }
                // Before there was an edge choice, the soft edge and the outline could both be on (0 meaning off).
                if (!edgeSaved) s.StripEdge = s.StripBorder > 0 ? 2 : s.StripFeather > 0 ? 1 : 0;
            }
            catch { }
            s.Clamp();
            return s;
        }

        public void Save()
        {
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(FilePath));
                var sb = new StringBuilder();
                foreach (FieldInfo f in Fields)
                    sb.AppendLine(f.Name + "=" + Convert.ToString(f.GetValue(this), CultureInfo.InvariantCulture));
                File.WriteAllText(FilePath, sb.ToString());
            }
            catch { }
        }

        public void CopyFrom(Settings other)
        {
            foreach (FieldInfo f in Fields) f.SetValue(this, f.GetValue(other));
        }

        void Clamp()
        {
            Brightness = Fit(Brightness, 20, 100);
            BlueFilter = Fit(BlueFilter, CoolRange ? -75 : 0, 100);
            ColorEffect = Fit(ColorEffect, 0, 4);
            GrayStyle = Fit(GrayStyle, 0, 2);
            Mode = Fit(Mode, -1, 7);
            KeptBrightness = Fit(KeptBrightness, 20, 100);
            KeptBlue = Fit(KeptBlue, -75, 100);
            KeptEffect = Fit(KeptEffect, 0, 4);
            FocusWork = Fit(FocusWork, 5, 90);
            FocusRest = Fit(FocusRest, 1, 30);
            BlinkInterval = Fit(BlinkInterval, 3, 120);
            IconSize = Fit(IconSize, 32, 256);
            IconOpacity = Fit(IconOpacity, 10, 100);
            IconCorner = Fit(IconCorner, 0, 4);
            BreakInterval = Fit(BreakInterval, 5, 60);
            BreakDuration = Fit(BreakDuration, 10, 120);
            HardwareRestore = Fit(HardwareRestore, -1, 100);
            if (HardwareBrightness && Backlight < 0)
            {
                // Before the backlight had its own slider, Brightness drove it in this mode.
                Backlight = Brightness;
                Brightness = 100;
            }
            Backlight = Fit(Backlight, -1, 100);
            NightFade = Fit(NightFade, 0, 180);
            BlinkFadeDepth = Fit(BlinkFadeDepth, 10, 100);
            BlinkStyle = Fit(BlinkStyle, 0, 1);
            ToneMode = Fit(ToneMode, 0, 4);
            ComfortLevel = Fit(ComfortLevel, 0, CustomLevel);
            ShadowCrush = Fit(ShadowCrush, 0, 100);
            Squeeze = Fit(Squeeze, 0, 100);
            LimiterStrength = Fit(LimiterStrength, 0, 100);
            if (ToneMode == 1)
            {
                // The old manual mode is the standard engine with custom strength; all sliders at zero meant no tone.
                ToneMode = 0;
                ComfortLevel = CustomLevel;
                if (ShadowCrush == 0 && Squeeze == 0 && LimiterStrength == 0) ToneEnabled = false;
            }
            if (ToneMode != 0 && ComfortLevel == CustomLevel) ComfortLevel = 1;
            ScheduleMode = Fit(ScheduleMode, 0, 1);
            NightFrom = Fit(NightFrom, 0, 1439);
            NightTo = Fit(NightTo, 0, 1439);
            StripHeight = Fit(StripHeight, 40, 400);
            StripDim = Fit(StripDim, 30, 95);
            GrainStrength = Fit(GrainStrength, 5, 100);
            GrainTone = Fit(GrainTone, 0, 2);
            StripEdge = Fit(StripEdge, 0, 2);
            StripFeather = Fit(StripFeather, 4, 200);
            StripShape = StripShape == 4 ? 0 : Fit(StripShape, 0, 3);   // 4 was "fill"
            StripWidth = Fit(StripWidth, 80, 2000);
            StripCover = Fit(StripCover, 0, 2);
            if (StripColor >= 0 && StripColor < Theme.StripColors.Length) StripColor = Theme.StripColors[StripColor].ToArgb();
            StripBorder = Fit(StripBorder, 1, 12);
            StripAccel = Fit(StripAccel, 0, 100);
            LongBreakInterval = Fit(LongBreakInterval, 30, 120);
            LongBreakDuration = Fit(LongBreakDuration, 1, 15);
            MiniFadeDelay = Fit(MiniFadeDelay, 10, 1000);
            MiniFadeTime = Fit(MiniFadeTime, 0, 200);
            NightBlue = Fit(NightBlue, 0, 100);
            MiniIcon = Fit(MiniIcon, 0, 3);
            MiniOrbitSeconds = Fit(MiniOrbitSeconds, 0, 200);
            if (AccentColor >= 0 && AccentColor < Theme.AccentColors.Length) AccentColor = Theme.AccentColors[AccentColor].ToArgb();
            if (MiniOrbitColor > 0 && MiniOrbitColor < 6) MiniOrbitColor = Theme.OrbitColors[MiniOrbitColor].ToArgb();
            CloseAction = Fit(CloseAction, 0, 2);
            if (SettingsHeight != 0) SettingsHeight = Fit(SettingsHeight, 360, 2000);
        }

        static int Fit(int v, int lo, int hi) { return Math.Max(lo, Math.Min(hi, v)); }
    }
}
