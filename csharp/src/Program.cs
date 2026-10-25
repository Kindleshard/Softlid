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
    static class Program
    {
        [STAThread]
        static void Main(string[] args)
        {
            if (args.Length == 2 && args[0] == "--write-icon") { IconArt.WriteIco(args[1]); return; }
            if (args.Length == 1 && args[0] == "--check-filter") { Environment.Exit(ScreenFilter.SelfTest() ? 0 : 1); }
            if (args.Length == 3 && args[0] == "--smart-test")
            {
                SmartTest.Run(args[1], int.Parse(args[2]));
                return;
            }
            if (args.Length == 2 && args[0] == "--refresh-test") { SmartTest.RunRefresh(args[1]); return; }
            if (args.Length == 1 && args[0] == "--guided-preview")
            {
                // A guided look-away break on its own, with the saved settings (diagnostics and demos).
                Native.SetProcessDPIAware();
                Application.EnableVisualStyles();
                Settings saved = Settings.Load();
                Lang.Use(saved.Language);
                Application.Run(new GuidedBreak(saved));
                return;
            }
            if (args.Length == 3 && args[0] == "--plan-test")
            {
                // Diagnostics: the look-away schedule for a break length, and whether every line fits its step.
                var keys = new[] { "far", "sides", "updown", "circle", "close" };
                var plan = new SessionPlan(keys, "done", int.Parse(args[1]), 4.0, 1.0, true);
                var sb = new System.Text.StringBuilder();
                sb.AppendLine("minimum break with voice: " + Voice.MinimumSeconds(Voice.LookAwayKeys) + " s");
                for (int i = 0; i < keys.Length; i++)
                    sb.AppendLine(string.Format("{0,-7} starts {1,5:0.00}  lasts {2,5:0.00}  line {3,5:0.00}  fits {4}", keys[i], plan.Start[i], plan.Length[i],
                        Voice.Length(keys[i]), plan.Length[i] - (i == 0 ? plan.Lead : 0) >= Voice.Length(keys[i])));
                sb.AppendLine(string.Format("done    starts {0,5:0.00}  line {1,5:0.00}  ends {2,5:0.00}", plan.End, Voice.Length("done"), plan.End + Voice.Length("done")));
                System.IO.File.WriteAllText(args[2], sb.ToString());
                return;
            }
            if (args.Length == 1 && args[0] == "--stretch-preview")
            {
                Native.SetProcessDPIAware();
                Application.EnableVisualStyles();
                Settings saved = Settings.Load();
                Lang.Use(saved.Language);
                Application.Run(new StretchBreak(saved));
                return;
            }
            if (args.Length == 5 && args[0] == "--lut-guard")
            {
                LutGuard.RunHelper(args[1], args[2] == "1", int.Parse(args[3]), args[4]);
                return;
            }
            if (System.IO.Path.GetFileNameWithoutExtension(Application.ExecutablePath) == "LutStandIn")
            {
                // Tests only: a copy of the exe under this name behaves like DesktopLUT as far as the guard can tell.
                Application.Run(new Form { Text = "DesktopLUT stand-in", Width = 300, Height = 120 });
                return;
            }
            if (args.Length == 2 && args[0] == "--write-luts")
            {
                LutWriter.Run(args[1]);
                return;
            }
            if (args.Length >= 3 && args[0] == "--layer-test")
            {
                SmartTest.RunLayer(args[1], int.Parse(args[2]));
                return;
            }

            if (args.Length == 3 && args[0] == "--update-test") { Updater.Test(args[1], args[2]); return; }
            if (args.Length == 3 && args[0] == "--settings-shot")
            {
                // Diagnostics: every page of the settings window (Simple or Advanced view, "More options" all open), scrolled
                // through and saved as pictures, to check the layout. Run with BlinkBuddy closed; it may save its settings.
                Native.SetProcessDPIAware();
                Application.EnableVisualStyles();
                Application.SetCompatibleTextRenderingDefault(false);
                var app = new App(new EventWaitHandle(false, EventResetMode.AutoReset), false);
                app.S.AdvancedView = args[2] == "advanced";
                app.S.Folded = "";   // every section open
                using (var form = new SettingsForm(app)) form.Snapshot(args[1]);
                Environment.Exit(0);
            }
            Updater.AfterUpdate(args);
            bool first;
            using (var mutex = new Mutex(true, @"Local\BlinkBuddy.Instance", out first))
            using (var showEvent = new EventWaitHandle(false, EventResetMode.AutoReset, @"Local\BlinkBuddy.Show"))
            {
                if (!first)
                {
                    // Already running: ask that copy to bring up its settings window.
                    Native.AllowSetForegroundWindow(-1);
                    showEvent.Set();
                    return;
                }
                Native.SetProcessDPIAware();
                Application.EnableVisualStyles();
                Application.SetCompatibleTextRenderingDefault(false);
                Application.Run(new App(showEvent, Array.IndexOf(args, "--tray") < 0));
            }
        }
    }
}
