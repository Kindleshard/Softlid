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
    // Lives in the tray. One 1-second timer drives everything; nothing else runs between reminders.
    // This file has its setup, the tray menu, the tick, keep-awake, mini mode and exiting; the rest is in App.Screen.cs,
    // App.Breaks.cs and App.Shortcuts.cs.
    partial class App : ApplicationContext
    {
        const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
        const int AwaySeconds = 300;   // this long without keyboard/mouse input counts as a break
        // Ctrl+Alt+Shift + F: filter on/off, Up/Down: brightness, Right/Left: warmer/cooler, R: reading strip

        public readonly Settings S = Settings.Load();
        public readonly ScreenFilter Filter = new ScreenFilter();
        public readonly Icon WindowIcon;
        public string BacklightStatus = "";
        readonly float scale;
        readonly NotifyIcon tray = new NotifyIcon();
        ToolStripMenuItem filterItem, stripItem, pauseItem, resumeItem;
        readonly Timer tick = new Timer();
        readonly MessageWindow msg = new MessageWindow();
        readonly ReadingStrip strip = new ReadingStrip();
        readonly ToneMapper tone = new ToneMapper();
        readonly SmartFilter smart = new SmartFilter();
        readonly SmartLayer smartLayer = new SmartLayer();
        readonly LutGuard lut = new LutGuard();
        string lutKey;
        readonly Grain grain = new Grain();
        readonly Comfort comfort = new Comfort();
        float colourGain = 1f;   // overall dimming the colour effect applies right now
        readonly SynchronizationContext ui;
        EyePopup eye;
        Form card;   // the break showing now: a LookAwayCard or a GuidedBreak
        SettingsForm settings;
        int blinkLeft, breakElapsed, longElapsed, lastBacklight = -1, probeId, verifyTick;
        string toneKey = "0,0,0";
        float night = 1f;
        DateTime pausedUntil = DateTime.MinValue, previewUntil = DateTime.MinValue;
        bool dirty;

        public App(EventWaitHandle showEvent, bool openSettings)
        {
            Lang.Use(S.Language);
            Theme.UseAccent(S);
            KeepAwake.Set(S.KeepAwake);
            // Following Windows' accent: pick up a change made there (shown in windows opened from then on).
            SystemEvents.UserPreferenceChanged += (sender, e) => { if (S.AccentFromWindows && e.Category == UserPreferenceCategory.General) Theme.UseAccent(S); };
            using (Graphics g = Graphics.FromHwnd(IntPtr.Zero)) scale = g.DpiX / 96f;
            WindowIcon = IconArt.MakeIcon((int)(32 * scale));
            blinkLeft = S.BlinkInterval;

            msg.Hotkey = OnHotkey;
            msg.ShowRequest = ShowSettings;
            msg.Reapply = delegate
            {
                // Rebuild everything from scratch; displays (and their sizes) may have changed.
                smart.Stop();
                smartLayer.Stop();
                Filter.Off();
                tone.Reset();
                toneKey = null;
                grain.Invalidate();
                ApplyScreen();
            };
            smart.Failure = delegate { ApplyScreen(); };
            smartLayer.Failure = delegate { ApplyScreen(); };
            lut.Changed = delegate { ApplyScreen(); };
            lut.Confirmed = build =>
            {
                S.LutConfirmedBuild = build;   // no new check needed until Windows changes
                dirty = true;
            };
            comfort.Changed = delegate { ApplyScreen(); };
            comfort.CurrentGain = () => colourGain;
            RegisterHotkeys();
            frontWatch = new Modes.FrontWatch(CheckRules);
            Native.WTSRegisterSessionNotification(msg.Handle, 0);
            IntPtr hwnd = msg.Handle;
            ThreadPool.RegisterWaitForSingleObject(showEvent,
                delegate { Native.PostMessage(hwnd, MessageWindow.WM_SHOW, IntPtr.Zero, IntPtr.Zero); }, null, -1, false);

            ui = SynchronizationContext.Current ?? new WindowsFormsSynchronizationContext();
            BuildMenu();

            tray.Icon = IconArt.MakeIcon(SystemInformation.SmallIconSize.Width);
            tray.Text = "BlinkBuddy";
            tray.MouseMove += delegate
            {
                string text = "BlinkBuddy\n" + BreakStatus() + (S.LongBreakEnabled ? "\n" + LongBreakStatus() : "");
                if (FocusState != FocusOff) text = "BlinkBuddy\n" + Lang.F("Focus timer: {0}", FocusText());
                if (text.Length > 63) text = text.Substring(0, 63);   // the tray tooltip's limit
                if (tray.Text != text) tray.Text = text;
            };
            tray.MouseClick += (sender, e) => { if (e.Button == MouseButtons.Left) ShowSettings(); };
            tray.Visible = true;

            tone.ClearLeftover();
            ApplyScreen();
            if (S.HardwareBrightness) RefreshBacklightStatus();
            tick.Interval = 1000;
            tick.Tick += delegate { OnTick(); };
            tick.Start();

            if (openSettings) ShowSettings();
            else if (S.MiniMode && S.MiniEnabled) ShowMini();
            else TrimMemory();
            WeeklyUpdateCheck();
        }

        // The tray menu, in the current language (built again when the language changes).
        void BuildMenu()
        {
            var menu = new ContextMenuStrip { Renderer = new DarkMenuRenderer() };
            var open = new ToolStripMenuItem(Lang.T("Settings"), null, delegate { ShowSettings(); });
            open.Font = new Font(open.Font, FontStyle.Bold);
            filterItem = new ToolStripMenuItem(Lang.T("Screen filter"), null, delegate { OnHotkey(1); });
            filterItem.ShortcutKeyDisplayString = Hotkeys.Pretty(Hotkeys.Get(S, Hotkeys.Filter)).Replace(" ", "");
            stripItem = new ToolStripMenuItem(Lang.T("Reading strip"), null, delegate { OnHotkey(6); });
            stripItem.ShortcutKeyDisplayString = Hotkeys.Pretty(Hotkeys.Get(S, Hotkeys.Strip)).Replace(" ", "");
            pauseItem = new ToolStripMenuItem(Lang.T("Pause reminders"));
            pauseItem.DropDown.Renderer = menu.Renderer;
            pauseItem.DropDownItems.Add(Lang.T("For 30 minutes"), null, delegate { Pause(30); });
            pauseItem.DropDownItems.Add(Lang.T("For 1 hour"), null, delegate { Pause(60); });
            pauseItem.DropDownItems.Add(Lang.T("Until I resume"), null, delegate { Pause(-1); });
            resumeItem = new ToolStripMenuItem(Lang.T("Resume reminders"), null, delegate { pausedUntil = DateTime.MinValue; });
            // Screen modes, and keeping the PC awake (for a while, or until switched off).
            var modeItem = new ToolStripMenuItem(Lang.T("Screen mode"));
            modeItem.DropDown.Renderer = menu.Renderer;
            for (int i = 0; i <= Modes.Custom; i++)
            {
                int m = i;
                modeItem.DropDownItems.Add(new ToolStripMenuItem(Lang.T(Modes.Names[i]), null, delegate { ChooseMode(m); }));
            }
            var awakeItem = new ToolStripMenuItem(Lang.T("Keep PC awake"));
            awakeItem.DropDown.Renderer = menu.Renderer;
            var awakeOff = new ToolStripMenuItem(Lang.T("Off"), null, delegate { SetKeepAwake(false); });
            var awake1 = new ToolStripMenuItem(Lang.T("For 1 hour"), null, delegate { KeepAwakeFor(60); });
            var awake2 = new ToolStripMenuItem(Lang.T("For 2 hours"), null, delegate { KeepAwakeFor(120); });
            var awakeOn = new ToolStripMenuItem(Lang.T("Until I turn it off"), null, delegate { SetKeepAwake(true); });
            awakeItem.DropDownItems.AddRange(new ToolStripItem[] { awakeOff, awake1, awake2, awakeOn });
            menu.Opening += delegate
            {
                for (int i = 0; i < modeItem.DropDownItems.Count; i++) ((ToolStripMenuItem)modeItem.DropDownItems[i]).Checked = S.Mode == i;
                awakeItem.Checked = KeepingAwake;
                awakeOff.Checked = !KeepingAwake;
                awakeOn.Checked = S.KeepAwake;
            };
            modeItem.DropDown.Opened += delegate { Native.RoundCorners(modeItem.DropDown.Handle); };
            awakeItem.DropDown.Opened += delegate { Native.RoundCorners(awakeItem.DropDown.Handle); };
            menu.Items.AddRange(new ToolStripItem[]
            {
                open,
                new ToolStripMenuItem(Lang.T("Take a break now"), null, delegate { StartBreak(false); }),
                new ToolStripSeparator(),
                modeItem, filterItem, stripItem, awakeItem, pauseItem, resumeItem,
                new ToolStripSeparator(),
                new ToolStripMenuItem(Lang.T("Exit"), null, delegate { Exit(); })
            });
            ToolStripMenuItem pause = pauseItem;
            menu.Opening += delegate
            {
                filterItem.Checked = S.FilterEnabled;
                stripItem.Checked = S.StripEnabled;
                pauseItem.Visible = !Paused;
                resumeItem.Visible = Paused;
            };
            menu.Opened += delegate { Native.RoundCorners(menu.Handle); };
            pause.DropDown.Opened += delegate { Native.RoundCorners(pause.DropDown.Handle); };
            ContextMenuStrip old = tray.ContextMenuStrip;
            tray.ContextMenuStrip = menu;
            if (old != null) old.Dispose();
        }

        // A new language: the menu and any open window are built again in it.
        public void SetLanguage(string code)
        {
            S.Language = code;
            dirty = true;
            Lang.Use(code);
            Rebuild();
        }

        // A new accent colour: Windows' own, or one picked in Options.
        public void SetAccent(bool fromWindows, int color)
        {
            S.AccentFromWindows = fromWindows;
            S.AccentColor = color;
            dirty = true;
            Theme.UseAccent(S);
            Rebuild();
        }

        // The menu and any open settings window, built again (in place, on the options page).
        public void Rebuild()
        {
            BuildMenu();
            if (settings == null) return;
            Point at = settings.Location;
            settings.Close();   // its FormClosed handler clears the field
            ShowSettings(at);
            if (settings != null) settings.ShowOptions();
        }

        // Once a week (if allowed), a quiet look for a newer version; only a newer one is mentioned, from the tray.
        public Updater.Release NewRelease;
        void WeeklyUpdateCheck()
        {
            DateTime last;
            if (!S.UpdateCheck || !Updater.Available) return;
            if (DateTime.TryParse(S.LastUpdateCheck, CultureInfo.InvariantCulture, DateTimeStyles.None, out last) && (DateTime.Today - last).TotalDays < 7) return;
            Updater.Check((release, message, current) => ui.Post(delegate
            {
                S.LastUpdateCheck = DateTime.Today.ToString("yyyy-MM-dd", CultureInfo.InvariantCulture);
                dirty = true;
                if (release == null) return;
                NewRelease = release;
                tray.ShowBalloonTip(8000, Lang.F("BlinkBuddy {0} is available", release.Tag), Lang.T("Open BlinkBuddy options to install it."), ToolTipIcon.Info);
            }, null));
        }

        bool Paused { get { return DateTime.Now < pausedUntil; } }

        void OnTick()
        {
            if (dirty) { dirty = false; S.Save(); }
            UpdateLocation();
            // Each second: the night schedule moves on, or a preview of the night strength ends.
            float n = NightShown();
            if (Math.Abs(n - night) > 0.004f) ApplyScreen();
            if (tone.Active && ++verifyTick % 5 == 0) tone.Verify();
            lut.Poll();
            smart.KeepOnTop();
            smartLayer.KeepOnTop();
            CheckRules();
            if (awakeUntil != DateTime.MinValue && DateTime.Now >= awakeUntil)
            {
                awakeUntil = DateTime.MinValue;
                KeepAwake.Set(S.KeepAwake);
            }
            if (card != null || Paused) return;
            FocusTick();
            if (card != null) return;
            int idle = Native.IdleSeconds();
            if (idle >= AwaySeconds)
            {
                breakElapsed = 0;
                blinkLeft = S.BlinkInterval;
                if (idle >= S.LongBreakDuration * 60) longElapsed = 0;
                return;
            }
            // Breaks that fall due during a game/video wait until it ends. While the focus timer runs, it decides
            // when to rest instead.
            bool timers = FocusState == FocusOff;
            if (timers && S.LongBreakEnabled && ++longElapsed >= S.LongBreakInterval * 60)
            {
                longElapsed = S.LongBreakInterval * 60;
                if (!FullscreenBlocked()) { StartBreak(true); return; }
            }
            if (timers && S.BreakEnabled && ++breakElapsed >= S.BreakInterval * 60)
            {
                breakElapsed = S.BreakInterval * 60;
                // A stand-up break is moments away; let it cover this one.
                if (S.LongBreakEnabled && S.LongBreakInterval * 60 - longElapsed <= 120) breakElapsed = 0;
                else if (!FullscreenBlocked()) { StartBreak(false); return; }
            }
            if (S.BlinkEnabled && --blinkLeft <= 0)
            {
                blinkLeft = S.BlinkInterval;
                if (!FullscreenBlocked()) Blink(false);
            }
        }

        bool FullscreenBlocked() { return S.PauseInFullscreen && Native.FullscreenAppRunning(); }

        // ---- Keep the PC awake ----

        DateTime awakeUntil = DateTime.MinValue;   // a timed keep-awake from the tray
        public bool KeepingAwake { get { return S.KeepAwake || DateTime.Now < awakeUntil; } }

        public void SetKeepAwake(bool on)
        {
            S.KeepAwake = on;
            awakeUntil = DateTime.MinValue;
            dirty = true;
            KeepAwake.Set(on);
            if (settings != null) settings.SyncFromApp();
        }

        public void KeepAwakeFor(int minutes)
        {
            S.KeepAwake = false;
            awakeUntil = DateTime.Now.AddMinutes(minutes);
            dirty = true;
            KeepAwake.Set(true);
            if (settings != null) settings.SyncFromApp();
        }

        static string Clock(int seconds)
        {
            seconds = Math.Max(0, seconds);
            return string.Format("{0}:{1:00}", seconds / 60, seconds % 60);
        }

        void Pause(int minutes) { pausedUntil = minutes < 0 ? DateTime.MaxValue : DateTime.Now.AddMinutes(minutes); }

        // Called by the settings window after any change; the file is written on the next tick.
        public void Changed(bool screen)
        {
            dirty = true;
            if (screen) ApplyScreen();
            blinkLeft = Math.Min(blinkLeft, S.BlinkInterval);
        }

        public bool RunAtStartup
        {
            get
            {
                using (RegistryKey k = Registry.CurrentUser.OpenSubKey(RunKey))
                    return k != null && k.GetValue("BlinkBuddy") != null;
            }
            set
            {
                using (RegistryKey k = Registry.CurrentUser.CreateSubKey(RunKey))
                {
                    if (value) k.SetValue("BlinkBuddy", "\"" + Application.ExecutablePath + "\" --tray");
                    else k.DeleteValue("BlinkBuddy", false);
                }
            }
        }

        public void ShowSettings() { ShowSettings(null); }

        // Mini mode: the settings window folds into a small eye that stays on top (see MiniWindow).
        MiniWindow mini;

        // Mini mode on or off in Options: off, it never appears (and is closed if it was showing).
        public void SetMiniEnabled(bool on)
        {
            S.MiniEnabled = on;
            dirty = true;
            if (on || mini == null) return;
            S.MiniMode = false;
            mini.Close();
            mini = null;
        }

        public void EnterMiniMode()
        {
            if (!S.MiniEnabled) return;
            S.MiniMode = true;
            dirty = true;
            if (settings != null) settings.Close();
            ShowMini();
        }

        void ShowMini()
        {
            if (mini == null || mini.IsDisposed) mini = new MiniWindow(this);
            mini.Show();
        }

        public void LeaveMiniMode()
        {
            S.MiniMode = false;
            dirty = true;
            if (mini != null) mini.Close();
            mini = null;
            ShowSettings();
        }

        public void SaveMiniPosition(Point at)
        {
            S.MiniX = at.X;
            S.MiniY = at.Y;
            dirty = true;
        }

        void ShowSettings(Point? at)
        {
            if (mini != null)
            {
                // Opening the settings (tray, hotkey) unfolds mini mode.
                S.MiniMode = false;
                dirty = true;
                mini.Close();
                mini = null;
            }
            if (settings == null)
            {
                settings = new SettingsForm(this);
                if (at.HasValue)
                {
                    settings.StartPosition = FormStartPosition.Manual;
                    settings.Location = at.Value;
                }
                settings.FormClosed += delegate
                {
                    settings = null;
                    S.Save();
                    dirty = false;
                    TrimMemory();
                };
                settings.Show();
            }
            if (settings.WindowState == FormWindowState.Minimized) settings.WindowState = FormWindowState.Normal;
            settings.Activate();
        }

        public void ResetAll()
        {
            if (S.HardwareBrightness) SetHardwareBrightness(false, delegate { });
            S.CopyFrom(new Settings());
            Changed(true);
            blinkLeft = S.BlinkInterval;
            breakElapsed = longElapsed = 0;
            if (settings == null) return;
            Point at = settings.Location;
            settings.Close();   // its FormClosed handler clears the field
            ShowSettings(at);
        }

        // Exit chosen when closing the settings window.
        public void Quit()
        {
            if (settings != null) settings.Hide();
            Exit();
        }

        // After an update is in place: start the new copy (it waits for this one), then quit.
        public void Restart(Action startNew)
        {
            startNew();
            Exit();
        }

        void Exit()
        {
            tick.Stop();
            tray.Visible = false;
            if (mini != null) mini.Close();
            if (lids != null) lids.Dispose();
            S.Save();
            smart.Dispose();
            smartLayer.Dispose();
            lut.Dispose();
            Filter.Dispose();
            tone.Dispose();
            strip.Dispose();
            frontWatch.Dispose();
            grain.Dispose();
            comfort.Dispose();
            for (int i = 1; i <= Hotkeys.Count; i++) Native.UnregisterHotKey(msg.Handle, i);
            KeepAwake.Set(false);
            Native.WTSUnRegisterSessionNotification(msg.Handle);
            msg.DestroyHandle();
            ExitThread();
        }

        // Hand unused pages back to Windows after the settings window or a break card closes.
        static void TrimMemory()
        {
            GC.Collect();
            GC.WaitForPendingFinalizers();
            GC.Collect();
            Native.TrimWorkingSet();
        }
    }

    // Hidden window that receives global hotkeys, "show settings" requests, display changes and unlocks.
    class MessageWindow : NativeWindow
    {
        public const int WM_SHOW = 0x8001;
        const int WM_DISPLAYCHANGE = 0x7E, WM_HOTKEY = 0x312, WM_WTSSESSION_CHANGE = 0x2B1, WTS_SESSION_UNLOCK = 0x8;
        public Action<int> Hotkey;
        public Action ShowRequest, Reapply;

        public MessageWindow() { CreateHandle(new CreateParams()); }

        protected override void WndProc(ref Message m)
        {
            if (m.Msg == WM_HOTKEY && Hotkey != null) Hotkey((int)m.WParam);
            else if (m.Msg == WM_SHOW && ShowRequest != null) ShowRequest();
            else if ((m.Msg == WM_DISPLAYCHANGE || (m.Msg == WM_WTSSESSION_CHANGE && (int)m.WParam == WTS_SESSION_UNLOCK)) && Reapply != null) Reapply();
            base.WndProc(ref m);
        }
    }
}
