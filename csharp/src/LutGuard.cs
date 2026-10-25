using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;
using Microsoft.Win32;
using Timer = System.Windows.Forms.Timer;

namespace BlinkBuddy
{
    // External LUT mode: DesktopLUT applies the colour table inside Windows' compositor, and BlinkBuddy watches over it
    // from the outside. DesktopLUT runs as administrator, so the watching is done by a small elevated helper (this same
    // exe with --lut-guard) that starts DesktopLUT itself and can always stop it. Stopping DesktopLUT removes the LUT
    // (its hook unloads when its process ends), so every safeguard ends the same way: stop it, and BlinkBuddy falls
    // back to its own filter.
    //   - Trial: after switching on, and after any Windows update, the screen must be confirmed within 15 s or it is undone.
    //   - Watchdog: DesktopLUT exiting unexpectedly, the compositor restarting twice within a minute, or BlinkBuddy
    //     itself closing or crashing stops it.
    //   - Panic hotkey: Ctrl+Alt+Shift+L stops it at any time, even when nothing on screen can be seen.
    class LutGuard : IDisposable
    {
        const string StopEventName = @"Local\BlinkBuddy.LutStop";
        const string WakeEventName = @"Local\BlinkBuddy.LutWake";

        Process helper;
        string lutName = "DesktopLUT";
        int commandSeq;
        EventWaitHandle stopEvent, wakeEvent;
        DateTime statusSeen;
        bool stopping;
        public bool Failed { get; private set; }
        public string Status = "";
        public Action Changed;            // the guard failed, stopped or was confirmed
        public Action<string> Confirmed;  // receives the Windows build the trial was confirmed on

        static string StatusFile
        {
            get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "BlinkBuddy", "lut-guard.status"); }
        }

        // The LUT built from the DesktopLUT sliders; DesktopLUT is pointed at it every time the guard starts it.
        public static string CubePath
        {
            get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "BlinkBuddy", "DesktopLUT.cube"); }
        }

        public static string WindowsBuild()
        {
            using (RegistryKey k = Registry.LocalMachine.OpenSubKey(@"SOFTWARE\Microsoft\Windows NT\CurrentVersion"))
                return k == null ? "" : k.GetValue("CurrentBuild") + "." + k.GetValue("UBR");
        }

        public bool Running { get { return helper != null && !helper.HasExited; } }

        public void Ensure(Settings s, bool forceTrial)
        {
            if (Running || Failed) return;
            if (!File.Exists(s.LutPath))
            {
                Fail("DesktopLUT wasn't found at " + s.LutPath + ".", false);
                return;
            }
            lutName = Path.GetFileNameWithoutExtension(s.LutPath);
            bool trial = forceTrial || s.LutConfirmedBuild != WindowsBuild();
            if (stopEvent == null) stopEvent = new EventWaitHandle(false, EventResetMode.ManualReset, StopEventName);
            stopEvent.Reset();
            if (wakeEvent == null) wakeEvent = new EventWaitHandle(false, EventResetMode.AutoReset, WakeEventName);
            wakeEvent.Reset();
            try { File.Delete(StatusFile); } catch { }
            try { File.Delete(CommandFile); } catch { }
            statusSeen = DateTime.MinValue;
            var psi = new ProcessStartInfo(Application.ExecutablePath,
                string.Format("--lut-guard \"{0}\" {1} {2} \"{3}\"", s.LutPath, trial ? 1 : 0, Process.GetCurrentProcess().Id, CubePath)) { UseShellExecute = true };
            if (Environment.GetEnvironmentVariable("BLINKBUDDY_LUT_NOELEVATE") != "1") psi.Verb = "runas";   // tests only
            try
            {
                helper = Process.Start(psi);
                stopping = false;
                Status = trial ? "Starting DesktopLUT for a 15-second check..." : "Starting DesktopLUT...";
            }
            catch (Win32Exception ex)
            {
                Fail(ex.NativeErrorCode == 1223
                    ? "Administrator permission was declined, so DesktopLUT wasn't started."
                    : "Couldn't start the guard: " + ex.Message, false);
            }
        }

        // Called once a second by the app.
        public void Poll()
        {
            if (helper == null) return;
            ReadStatus();
            if (helper == null || !helper.HasExited) return;   // a reported failure may already have stopped it
            ReadStatus();
            if (helper == null) return;
            helper.Dispose();
            helper = null;
            if (!stopping)
            {
                // The guard was killed before it could stop DesktopLUT (its job normally takes DesktopLUT with it).
                // Try to stop it from here too; this works unless DesktopLUT refuses a non-administrator.
                foreach (Process p in Process.GetProcessesByName(lutName))
                    try { p.Kill(); } catch { }
                ScreenRefresh.After(300, 1000, 2500);   // the guard didn't get to clear the old picture away
                if (!Failed) Fail("The guard closed unexpectedly, so DesktopLUT was stopped and BlinkBuddy's own filter took over.");
            }
            stopping = false;
            if (Changed != null) Changed();
        }

        void ReadStatus()
        {
            try
            {
                var info = new FileInfo(StatusFile);
                if (!info.Exists || info.LastWriteTimeUtc <= statusSeen) return;
                statusSeen = info.LastWriteTimeUtc;
                string[] parts = File.ReadAllText(StatusFile).Trim().Split(new[] { '|' }, 3);
                if (parts.Length < 2) return;
                Status = parts[1];
                if (parts[0] == "confirmed" && parts.Length == 3 && Confirmed != null) Confirmed(parts[2]);
                if (parts[0] == "failed" && !stopping) Fail(parts[1]);
            }
            catch { }
        }

        // notify is false inside Ensure: the caller is already applying the screen and checks Failed afterwards.
        void Fail(string why, bool notify = true)
        {
            Failed = true;
            Status = why;
            if (notify && Changed != null) Changed();
        }

        public void ClearFailure()
        {
            Failed = false;
            Status = "";
        }

        public void Stop()
        {
            if (helper == null) return;
            stopping = true;
            if (stopEvent != null) stopEvent.Set();
            if (helper.WaitForExit(6000))
            {
                helper.Dispose();
                helper = null;
            }
            if (!Failed) Status = "";   // keep the reason it failed on show
        }

        // The LUT was rewritten (a new strength): the guard restarts DesktopLUT so it loads it (no new permission prompt
        // or check).
        public void Reload()
        {
            if (Command("run")) Status = "Applying the new strength...";
        }

        // "run <n>" in the command file, then a wake-up. The guard acts on the newest command only, so a quick run of
        // strength changes restarts DesktopLUT for the last one rather than for each.
        bool Command(string what)
        {
            if (!Running || wakeEvent == null) return false;
            try { File.WriteAllText(CommandFile, what + " " + (++commandSeq)); }
            catch { return false; }
            wakeEvent.Set();
            return true;
        }

        static string CommandFile
        {
            get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "BlinkBuddy", "lut-guard.command"); }
        }

        public void Dispose()
        {
            Stop();
            if (stopEvent != null) stopEvent.Dispose();
            if (wakeEvent != null) wakeEvent.Dispose();
        }

        // ---- The elevated helper: BlinkBuddy.exe --lut-guard "<DesktopLUT.exe>" <trial 0/1> <BlinkBuddy pid> "<cube>" ----

        public static void RunHelper(string lutPath, bool trial, int ownerPid, string cubePath)
        {
            Native.SetProcessDPIAware();
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new Helper(lutPath, trial, ownerPid, cubePath));
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
        static extern bool WritePrivateProfileString(string section, string key, string value, string file);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern IntPtr CreateJobObject(IntPtr attributes, string name);
        [DllImport("kernel32.dll")] static extern bool SetInformationJobObject(IntPtr job, int infoClass, ref JobLimits info, int size);
        [DllImport("kernel32.dll")] static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);

        // JOBOBJECT_EXTENDED_LIMIT_INFORMATION; only LimitFlags is used.
        [StructLayout(LayoutKind.Sequential)]
        struct JobLimits
        {
            public long PerProcessUserTimeLimit, PerJobUserTimeLimit;
            public uint LimitFlags;
            public UIntPtr MinimumWorkingSetSize, MaximumWorkingSetSize;
            public uint ActiveProcessLimit;
            public UIntPtr Affinity;
            public uint PriorityClass, SchedulingClass;
            public ulong ReadOps, WriteOps, OtherOps, ReadBytes, WriteBytes, OtherBytes;
            public UIntPtr ProcessMemoryLimit, JobMemoryLimit, PeakProcessMemoryUsed, PeakJobMemoryUsed;
        }

        // A job that Windows closes when this process ends in any way, ending everything in it.
        static IntPtr KillOnExitJob()
        {
            IntPtr job = CreateJobObject(IntPtr.Zero, null);
            if (job == IntPtr.Zero) return job;
            var limits = new JobLimits { LimitFlags = 0x2000 };   // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
            SetInformationJobObject(job, 9, ref limits, Marshal.SizeOf(typeof(JobLimits)));   // JobObjectExtendedLimitInformation
            return job;
        }

        // DesktopLUT doesn't say when its new table is in place (its hook rebuilds itself inside the compositor after
        // the program starts), so refresh every 150 ms for the first 3 s, then once more as a backstop.
        static readonly int[] StartupRefreshes = Enumerable.Range(1, 20).Select(i => i * 150).Concat(new[] { 5000 }).ToArray();

        class Helper : ApplicationContext
        {
            const int PanicHotkey = 0x4C;   // L, with Ctrl+Alt+Shift
            readonly string lutPath, lutName, cubePath;
            readonly Timer watch = new Timer { Interval = 1000 };
            RegisteredWaitHandle wakeWait;
            int lastCommand;
            readonly HotkeyWindow keys = new HotkeyWindow();
            readonly EventWaitHandle stopEvent, wakeEvent;
            readonly Process owner;
            readonly IntPtr job = KillOnExitJob();   // so DesktopLUT can't outlive the guard, even if the guard is killed
            int dwmPid;
            DateTime[] dwmRestarts = new DateTime[0];
            bool done;

            public Helper(string path, bool trial, int ownerPid, string cube)
            {
                try { owner = Process.GetProcessById(ownerPid); }
                catch { owner = null; }
                lutPath = path;
                lutName = Path.GetFileNameWithoutExtension(path);
                cubePath = cube;
                try { stopEvent = EventWaitHandle.OpenExisting(StopEventName); }
                catch { stopEvent = new EventWaitHandle(false, EventResetMode.ManualReset, StopEventName); }
                try { wakeEvent = EventWaitHandle.OpenExisting(WakeEventName); }
                catch { wakeEvent = new EventWaitHandle(false, EventResetMode.AutoReset, WakeEventName); }
                keys.Pressed = delegate { Finish("failed", "Stopped with Ctrl+Alt+Shift+L. BlinkBuddy's own filter took over."); };
                Native.RegisterHotKey(keys.Handle, 1, Native.MOD_CONTROL | Native.MOD_ALT | Native.MOD_SHIFT | Native.MOD_NOREPEAT, PanicHotkey);
                dwmPid = DwmPid();
                // Start once the message loop runs, so a failure can close the helper cleanly.
                var start = new Timer { Interval = 10 };
                start.Tick += delegate
                {
                    start.Dispose();
                    Start(trial);
                };
                start.Start();
            }

            void Start(bool trial)
            {
                if (!Launch()) return;
                Write("running", trial ? "Checking: confirm within 15 seconds that the screen looks right." : "DesktopLUT is running, watched by BlinkBuddy.");
                watch.Tick += delegate { Check(); };
                watch.Start();
                // Commands from BlinkBuddy: "run" restarts DesktopLUT (the only way to make it load a changed LUT).
                // Handled the moment they arrive, on this thread, so the watchdog can't mistake the moment in between
                // for a crash.
                SynchronizationContext ui = SynchronizationContext.Current;
                wakeWait = ThreadPool.RegisterWaitForSingleObject(wakeEvent, delegate
                {
                    ui.Post(delegate { OnCommand(); }, null);
                }, null, -1, false);
                if (trial)
                {
                    var check = new TrialForm();
                    check.FormClosed += delegate
                    {
                        if (done) return;
                        if (check.Kept) Write("confirmed", "DesktopLUT is running, watched by BlinkBuddy (confirmed on this Windows build).", WindowsBuild());
                        else Finish("failed", check.TimedOut
                            ? "No confirmation within 15 seconds, so DesktopLUT was stopped and BlinkBuddy's own filter took over."
                            : "Undone. DesktopLUT was stopped and BlinkBuddy's own filter took over.");
                    };
                    check.Show();
                }
            }

            // (Re)start DesktopLUT pointed at BlinkBuddy's LUT. A copy that is already running is closed first, so it
            // can't save its old LUT over ours on the way out.
            bool Launch()
            {
                CloseLut();
                try
                {
                    string ini = Path.Combine(Path.GetDirectoryName(lutPath), "DesktopLUT.ini");
                    WritePrivateProfileString("General", "StartMinimized", "true", ini);   // no window popping up on restarts
                    for (int i = 0; i < Screen.AllScreens.Length; i++)
                        WritePrivateProfileString("Monitor" + i, "LUT_SDR", cubePath, ini);
                    using (Process p = Process.Start(new ProcessStartInfo(lutPath) { UseShellExecute = true, WorkingDirectory = Path.GetDirectoryName(lutPath) }))
                        if (p != null && job != IntPtr.Zero) AssignProcessToJobObject(job, p.Handle);
                    ScreenRefresh.After(StartupRefreshes);   // so the new table covers the whole screen
                    return true;
                }
                catch (Exception ex)
                {
                    Finish("failed", "DesktopLUT couldn't be started: " + ex.Message);
                    return false;
                }
            }

            void CloseLut()
            {
                foreach (Process p in Processes())
                    try
                    {
                        // Minimised to the tray it has no window to close, so go straight to ending it.
                        if (!p.CloseMainWindow() || !p.WaitForExit(2000)) p.Kill();
                        p.WaitForExit(3000);
                    }
                    catch { }
            }

            void OnCommand()
            {
                if (done) return;
                string[] parts;
                try { parts = File.ReadAllText(CommandFile).Trim().Split(' '); }
                catch { return; }
                int seq;
                if (parts.Length != 2 || !int.TryParse(parts[1], out seq) || seq == lastCommand) return;
                lastCommand = seq;
                if (parts[0] == "run" && Launch()) Write("running", "DesktopLUT is running, watched by BlinkBuddy.");
            }

            Process[] Processes() { return Process.GetProcessesByName(lutName); }

            static int DwmPid()
            {
                int session = Process.GetCurrentProcess().SessionId;
                Process dwm = Process.GetProcessesByName("dwm").FirstOrDefault(p => p.SessionId == session);
                return dwm == null ? 0 : dwm.Id;
            }

            void Check()
            {
                if (done) return;
                if (stopEvent.WaitOne(0))
                {
                    Finish("stopped", "DesktopLUT was stopped.");
                    return;
                }
                if (owner == null || owner.HasExited)
                {
                    Finish("stopped", "BlinkBuddy closed, so DesktopLUT was stopped.");
                    return;
                }
                if (Processes().Length == 0)
                {
                    Finish("failed", "DesktopLUT closed or crashed, so BlinkBuddy's own filter took over.");
                    return;
                }
                // A compositor restart can happen for harmless reasons (a driver update); two within a minute means trouble.
                int pid = DwmPid();
                if (pid != 0 && pid != dwmPid)
                {
                    dwmPid = pid;
                    dwmRestarts = dwmRestarts.Where(t => (DateTime.Now - t).TotalSeconds < 60).Concat(new[] { DateTime.Now }).ToArray();
                    if (dwmRestarts.Length >= 2)
                        Finish("failed", "Windows' compositor restarted twice within a minute, so DesktopLUT was stopped as a precaution.");
                }
            }

            // Every way out ends here: stop DesktopLUT (which removes its LUT), report, and close.
            void Finish(string state, string message)
            {
                if (done) return;
                done = true;
                watch.Stop();
                if (wakeWait != null) wakeWait.Unregister(null);
                CloseLut();
                // DesktopLUT's hook lets go a moment after it ends; refresh after that so no part of the screen keeps
                // the LUT's picture.
                try
                {
                    Thread.Sleep(400);
                    ScreenRefresh.Now();
                    Thread.Sleep(800);
                    ScreenRefresh.Now();
                }
                catch { }
                Write(state, message);
                Native.UnregisterHotKey(keys.Handle, 1);
                keys.DestroyHandle();
                ExitThread();
            }

            static void Write(string state, string message, string extra = "")
            {
                try
                {
                    Directory.CreateDirectory(Path.GetDirectoryName(StatusFile));
                    File.WriteAllText(StatusFile, state + "|" + message + (extra.Length > 0 ? "|" + extra : ""));
                }
                catch { }
            }
        }

        // Windows recomposes only the parts of the screen that change, so a LUT switched on, changed or off would show
        // only where something moves (around the mouse) and the rest would keep the old picture. Covering the screen for
        // a moment with an all-but-invisible window makes Windows recompose all of it.
        public static class ScreenRefresh
        {
            [DllImport("dwmapi.dll")] static extern int DwmFlush();
            static Sheet sheet;
            static Timer timer;
            static int[] pending;
            static int step;

            public static void Now()
            {
                if (sheet == null) sheet = new Sheet();
                sheet.Bounds = SystemInformation.VirtualScreen;
                sheet.Show();
                DwmFlush();   // wait for the frames that include it, then for the ones without it
                DwmFlush();
                sheet.Hide();
                DwmFlush();
            }

            // Refresh at each of these times (ms from now); a new call replaces the refreshes still waiting. DesktopLUT
            // doesn't say when its table is in place, so callers spread a few over its start-up.
            public static void After(params int[] times)
            {
                if (timer == null)
                {
                    timer = new Timer();
                    timer.Tick += delegate
                    {
                        timer.Stop();
                        try { Now(); } catch { }
                        if (++step < pending.Length)
                        {
                            timer.Interval = pending[step] - pending[step - 1];
                            timer.Start();
                        }
                    };
                }
                timer.Stop();
                pending = times;
                step = 0;
                timer.Interval = times[0];
                timer.Start();
            }

            class Sheet : Form
            {
                public Sheet()
                {
                    FormBorderStyle = FormBorderStyle.None;
                    ShowInTaskbar = false;
                    StartPosition = FormStartPosition.Manual;
                    BackColor = Color.Black;
                    Opacity = 0.01;   // darkens by 1% for two frames: invisible, but Windows still redraws everything under it
                    TopMost = true;
                }

                protected override bool ShowWithoutActivation { get { return true; } }

                protected override CreateParams CreateParams
                {
                    get
                    {
                        CreateParams cp = base.CreateParams;
                        cp.ExStyle |= 0x20 | 0x80 | 0x08000000;   // click-through, no taskbar button, never takes focus
                        return cp;
                    }
                }
            }
        }

        class HotkeyWindow : NativeWindow
        {
            public Action Pressed;
            public HotkeyWindow() { CreateHandle(new CreateParams()); }

            protected override void WndProc(ref Message m)
            {
                if (m.Msg == 0x312 && Pressed != null) Pressed();   // WM_HOTKEY
                base.WndProc(ref m);
            }
        }

        // "Keep these display settings?" for DesktopLUT: it undoes itself unless someone who can see the screen says yes.
        class TrialForm : Form
        {
            const int Seconds = 15;
            readonly Timer timer = new Timer { Interval = 1000 };
            readonly Label countdown;
            int left = Seconds;
            public bool Kept, TimedOut;

            public TrialForm()
            {
                SuspendLayout();
                AutoScaleDimensions = new SizeF(96f, 96f);
                AutoScaleMode = AutoScaleMode.Dpi;
                Text = "BlinkBuddy: keep DesktopLUT?";
                FormBorderStyle = FormBorderStyle.FixedDialog;
                MaximizeBox = MinimizeBox = false;
                StartPosition = FormStartPosition.CenterScreen;
                TopMost = true;
                BackColor = Theme.Card;
                ForeColor = Theme.Fg;
                Font = Theme.Body;
                ClientSize = new Size(420, 170);
                Controls.Add(Theme.MakeLabel("Does your screen look right?", Theme.Heading, Theme.Fg, new Rectangle(20, 16, 380, 28)));
                countdown = Theme.MakeLabel("", Theme.Small, Theme.Dim, new Rectangle(20, 48, 380, 40));
                countdown.TextAlign = ContentAlignment.TopLeft;
                Controls.Add(countdown);
                Button keep = Theme.MakeButton("Keep", new Rectangle(196, 112, 96, 36));
                Button undo = Theme.MakeButton("Undo", new Rectangle(304, 112, 96, 36));
                keep.Click += delegate { Kept = true; Close(); };
                undo.Click += delegate { Close(); };
                Controls.Add(keep);
                Controls.Add(undo);
                AcceptButton = keep;
                CancelButton = undo;
                ResumeLayout(false);
                Tick();
                timer.Tick += delegate { Tick(); };
                timer.Start();
            }

            void Tick()
            {
                if (left <= 0)
                {
                    TimedOut = true;
                    Close();
                    return;
                }
                countdown.Text = string.Format("DesktopLUT is now applying the colour table. If you do nothing, it is undone in {0} s.\nPanic key at any time: Ctrl+Alt+Shift+L.", left--);
            }

            protected override void OnShown(EventArgs e)
            {
                base.OnShown(e);
                Activate();
            }

            protected override void OnFormClosed(FormClosedEventArgs e)
            {
                timer.Dispose();
                base.OnFormClosed(e);
            }
        }
    }
}
