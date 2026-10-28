// Every setting, once: its type, name and default. Settings.h turns this list into fields; Settings.cpp into reading
// and writing %AppData%\BlinkBuddy\settings.ini as "Name=Value" lines, in this order (the same file, and the same
// order, as the C# version). Types: bool, int, text (a string). Colours are ARGB in an int.
//
// (No include guard on purpose: it is included once for each thing made from the list.)

// Screen
BB_SETTING(bool, FilterEnabled, true)
BB_SETTING(int, Brightness, 100)                // %, 20..100
BB_SETTING(int, BlueFilter, 0)                  // %, 0..100 maps 6500 K -> 1900 K (below 0: cooler, with CoolRange)
BB_SETTING(bool, Invert, false)
BB_SETTING(int, ColorEffect, 0)                 // 0 none, 1 grayscale, 2 sepia, 3 e-ink, 4 red night
BB_SETTING(int, GrayStyle, 0)                   // grayscale as 0 Rec.709, 1 Rec.601, 2 black and white
BB_SETTING(bool, CoolRange, false)              // the blue light slider also goes cooler than daylight
BB_SETTING(int, Mode, -1)                       // the screen mode picked (-1 none yet, 7 Custom)
BB_SETTING(bool, KeptFilter, true)              // your own values, kept while another mode is in use
BB_SETTING(bool, KeptTone, true)
BB_SETTING(int, KeptBrightness, 100)
BB_SETTING(int, KeptBlue, 0)
BB_SETTING(int, KeptEffect, 0)
BB_SETTING(text, AppRules, L"")                 // per-app modes: app.exe=mode=path, separated by bars
BB_SETTING(bool, RulesEnabled, true)            // those modes apply (off: kept, but idle)
BB_SETTING(bool, KeepAwake, false)              // keep the PC from sleeping and the screen from turning off
BB_SETTING(text, Hotkeys, L"")                  // custom shortcuts, action=keys separated by semicolons
BB_SETTING(bool, ForceRest, false)              // breaks can't be skipped (no Skip, Snooze or Esc)
BB_SETTING(bool, MiniCountdown, true)           // the mini icon's rim shows the time to the next break
BB_SETTING(int, FocusWork, 25)                  // min: the focus timer's work period
BB_SETTING(int, FocusRest, 5)                   // min: and its rest
BB_SETTING(bool, FocusEnabled, true)            // the focus timer is offered (off: stopped, and its card hidden)
BB_SETTING(bool, HardwareBrightness, false)     // also drive the display's real backlight
BB_SETTING(int, Backlight, -1)                  // %, that backlight level (separate from Brightness)
BB_SETTING(int, HardwareRestore, -1)            // backlight % to return to when that is switched off

// Tone
BB_SETTING(bool, ToneEnabled, true)
BB_SETTING(int, ToneMode, 0)                    // engine: 0 standard, 2 smart GPU, 3 smart layer, 4 DesktopLUT
BB_SETTING(text, LutPath, L"C:\\Tools\\DesktopLUT\\DesktopLUT.exe")
BB_SETTING(text, LutConfirmedBuild, L"")        // Windows build the DesktopLUT trial was last confirmed on
BB_SETTING(int, ComfortLevel, 1)                // 0 gentle, 1 balanced, 2 strong, 3 max, 4 custom (standard only)
BB_SETTING(bool, AdaptToContent, true)
BB_SETTING(int, ShadowCrush, 0)                 // custom, %
BB_SETTING(int, Squeeze, 0)                     // custom, %
BB_SETTING(int, LimiterStrength, 0)             // custom, %

// Night schedule
BB_SETTING(bool, ScheduleEnabled, false)
BB_SETTING(int, ScheduleMode, 0)                // 0 sunset to sunrise, 1 custom hours
BB_SETTING(text, City, L"")                     // "lat, lon" from Windows location (or a city picked by hand)
BB_SETTING(bool, CityManual, false)
BB_SETTING(int, NightFade, 60)                  // min
BB_SETTING(int, NightFrom, 21 * 60)             // minutes after midnight
BB_SETTING(int, NightTo, 7 * 60)
BB_SETTING(int, NightBlue, 60)                  // %: the blue light filter at night
BB_SETTING(bool, NightLightSync, true)          // follow Windows Night light: its schedule, and it does the warming while on

// Reading strip
BB_SETTING(bool, StripEnabled, false)
BB_SETTING(int, StripHeight, 120)               // px at 100% scaling
BB_SETTING(int, StripDim, 70)                   // %
BB_SETTING(int, StripFeather, 60)               // px at 100%: the soft edge
BB_SETTING(int, StripShape, 0)                  // 0 band, 1 column, 2 rectangle, 3 circle
BB_SETTING(int, StripWidth, 640)                // px at 100%
BB_SETTING(int, StripCover, 0)                  // bands: 0 both sides, 1 above/left only, 2 below/right only
BB_SETTING(int, StripColor, 0)                  // ARGB (a small number is an old palette index)
BB_SETTING(int, StripEdge, 1)                   // 0 sharp, 1 soft, 2 outline
BB_SETTING(int, StripBorder, 2)                 // px: the outline's width
BB_SETTING(int, StripLine, 0)                   // the outline's colour (ARGB; 0 = the accent)
BB_SETTING(bool, StripFollow, true)
BB_SETTING(bool, StripOneMonitor, false)
BB_SETTING(int, StripAccel, 50)

// Film grain
BB_SETTING(bool, GrainEnabled, false)
BB_SETTING(int, GrainStrength, 25)
BB_SETTING(bool, GrainCoarse, false)
BB_SETTING(int, GrainTone, 1)
BB_SETTING(bool, GrainAnimated, false)

// Blink reminder
BB_SETTING(bool, BlinkEnabled, true)
BB_SETTING(int, BlinkInterval, 15)              // s
BB_SETTING(int, IconSize, 96)                   // px at 100%
BB_SETTING(int, IconOpacity, 70)                // %
BB_SETTING(int, IconCorner, 1)                  // 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right, 4 centre
BB_SETTING(int, BlinkStyle, 0)                  // 0 the eye icon, 1 eyelids over the whole screen
BB_SETTING(bool, BlinkFade, false)
BB_SETTING(int, BlinkFadeDepth, 100)            // %

// Look-away breaks (20-20-20)
BB_SETTING(bool, BreakEnabled, true)
BB_SETTING(int, BreakInterval, 20)              // min
BB_SETTING(int, BreakDuration, 20)              // s
BB_SETTING(bool, Sounds, true)
BB_SETTING(bool, Exercises, true)
BB_SETTING(bool, ExerciseVoice, false)
BB_SETTING(bool, StrictBreaks, false)
BB_SETTING(bool, BreakBackdrop, true)

// Stand-up breaks
BB_SETTING(bool, LongBreakEnabled, false)
BB_SETTING(int, LongBreakInterval, 60)          // min
BB_SETTING(int, LongBreakDuration, 5)           // min
BB_SETTING(bool, StretchGuided, true)
BB_SETTING(bool, StretchVoice, false)

// General
BB_SETTING(bool, PauseInFullscreen, true)
BB_SETTING(bool, AdvancedView, false)
BB_SETTING(bool, MiniMode, false)
BB_SETTING(int, MiniX, -1)
BB_SETTING(int, MiniY, -1)
BB_SETTING(int, MiniFadeDelay, 500)             // ms
BB_SETTING(int, MiniFadeTime, 150)              // ms
BB_SETTING(int, MiniOpacity, 65)               // %: how much of the icon shows while the pointer is away
BB_SETTING(int, MiniScale, 100)                 // %: the mini icon's size (90 to 150; replaced MiniSize, 1.3 times as big)
BB_SETTING(int, MiniFocusColor, 0)              // ARGB of the mini icon's focus timer circle (0 = green)
BB_SETTING(bool, MiniEnabled, true)
BB_SETTING(bool, MiniFold, true)                // the settings window folds into the icon, animated
BB_SETTING(bool, MiniOnMinimize, true)          // minimising the settings window switches to mini mode (not the taskbar)
BB_SETTING(int, MiniIcon, 0)                    // 0 eye, 1 lamp, 2 sun, 3 laptop
BB_SETTING(bool, MiniOrbit, false)
BB_SETTING(int, MiniOrbitSeconds, 60)
BB_SETTING(bool, AccentFromWindows, false)
BB_SETTING(int, AccentColor, Palette::Accents[0])   // ARGB (a small number is an old palette index)
BB_SETTING(int, MiniOrbitColor, 0)              // ARGB (0 = the accent; 1..5 an old palette index)
BB_SETTING(int, CloseAction, 0)                 // 0 ask, 1 keep running in the tray, 2 exit
BB_SETTING(text, PanelHidden, L"")              // features not on the panel (keys by commas; the focus timer: FocusEnabled)
BB_SETTING(text, PanelOrder, L"")               // the features on the settings panel, in order (keys by commas; see SettingsPanel)
BB_SETTING(text, Folded, L"")                   // settings sections folded shut, separated by bars
BB_SETTING(int, SettingsHeight, 0)              // the settings window's height at 100% (0 = standard)
BB_SETTING(int, SettingsZoom, 100)              // %: the settings window's size (80, 90, 100, 110 or 120)
BB_SETTING(text, Language, L"")                 // "" = the Windows language (if translated)
BB_SETTING(bool, UpdateCheck, true)
BB_SETTING(text, LastUpdateCheck, L"")          // yyyy-MM-dd
BB_SETTING(bool, TourDone, false)               // the first-run tour was seen (or skipped)
