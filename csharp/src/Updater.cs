using System;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Reflection;
using System.Text.RegularExpressions;
using System.Threading;
using System.Windows.Forms;

[assembly: AssemblyTitle("BlinkBuddy")]
[assembly: AssemblyProduct("BlinkBuddy")]
[assembly: AssemblyVersion("1.0.0.0")]
[assembly: AssemblyFileVersion("1.0.0.0")]

namespace BlinkBuddy
{
    // Checking for a newer BlinkBuddy: asks GitHub for the latest release of Repo, compares its tag ("v1.2.0") with this
    // copy's version and, if asked, downloads the release's BlinkBuddy.exe and swaps it in. A running program can be
    // renamed but not overwritten, so this copy steps aside as BlinkBuddy.exe.old, the new one takes its name, starts
    // (waiting for this one to quit first) and tidies the old file away.
    static class Updater
    {
        // Where releases are published, as "owner/name" on GitHub. Empty: no release channel yet.
        public const string Repo = "";

        public static Version Current { get { return typeof(Updater).Assembly.GetName().Version; } }
        public static string CurrentText { get { return Current.ToString(3); } }
        static string repo = Repo;   // (a diagnostic run can point it elsewhere)
        public static bool Available { get { return repo.Length > 0; } }

        public class Release
        {
            public Version Version;
            public string Tag, Download, Page;
        }

        // done(newer release or null, message, up to date) is called on a pool thread.
        public static void Check(Action<Release, string, bool> done)
        {
            if (!Available)
            {
                done(null, Lang.T("Updates aren't available yet: this copy of BlinkBuddy has no release channel."), false);
                return;
            }
            ThreadPool.QueueUserWorkItem(delegate
            {
                try
                {
                    ServicePointManager.SecurityProtocol |= SecurityProtocolType.Tls12;
                    string json;
                    using (var web = Client()) json = web.DownloadString("https://api.github.com/repos/" + repo + "/releases/latest");
                    Match tag = Regex.Match(json, "\"tag_name\"\\s*:\\s*\"v?([0-9][^\"]*)\"");
                    Match exe = Regex.Match(json, "\"browser_download_url\"\\s*:\\s*\"([^\"]+/BlinkBuddy\\.exe)\"");
                    Match page = Regex.Match(json, "\"html_url\"\\s*:\\s*\"([^\"]+/releases/[^\"]+)\"");
                    Version v;
                    if (!tag.Success || !Version.TryParse(Regex.Match(tag.Groups[1].Value, "^[0-9.]+").Value.TrimEnd('.'), out v))
                    {
                        done(null, Lang.T("Couldn't read the latest release's version."), false);
                        return;
                    }
                    if (Normal(v) <= Normal(Current))
                    {
                        done(null, Lang.T("You're up to date."), true);
                        return;
                    }
                    var r = new Release
                    {
                        Version = v,
                        Tag = tag.Groups[1].Value,
                        Download = exe.Success ? exe.Groups[1].Value : null,
                        Page = page.Success ? page.Groups[1].Value : "https://github.com/" + repo + "/releases/latest"
                    };
                    done(r, Lang.F("Version {0} is available (you have {1}).", r.Tag, CurrentText), false);
                }
                catch (Exception ex)
                {
                    done(null, Lang.F("Couldn't check for updates: {0}", ex.Message), false);
                }
            });
        }

        // Diagnostics: checks another repository's latest release and writes what it found to a file.
        public static void Test(string otherRepo, string file)
        {
            repo = otherRepo;
            var done = new ManualResetEvent(false);
            Check((r, message, current) =>
            {
                File.WriteAllText(file, message + (r == null ? "" : "\ntag " + r.Tag + ", version " + r.Version + ", exe " + (r.Download ?? "none") + ", page " + r.Page));
                done.Set();
            });
            done.WaitOne(30000);
        }

        static Version Normal(Version v)
        {
            return new Version(v.Major, v.Minor, Math.Max(0, v.Build), Math.Max(0, v.Revision));
        }

        // Downloads the new program and puts it in place of this one; ready() then starts it and quits (on the UI
        // thread). failed(message) and progress(fraction, text) are called on a pool thread.
        public static void Install(Release r, Action<double, string> progress, Action<Action> ready, Action<string> failed)
        {
            if (r.Download == null)
            {
                failed(Lang.F("This release has no BlinkBuddy.exe to install. Get it from {0}", r.Page));
                return;
            }
            ThreadPool.QueueUserWorkItem(delegate
            {
                string exe = Application.ExecutablePath, fresh = exe + ".new", old = exe + ".old";
                try
                {
                    ServicePointManager.SecurityProtocol |= SecurityProtocolType.Tls12;
                    using (var web = Client())
                    {
                        web.DownloadProgressChanged += (s, e) =>
                            progress(e.TotalBytesToReceive > 0 ? (double)e.BytesReceived / e.TotalBytesToReceive : 0, Lang.F("Downloading {0}: {1:0.0} of {2:0.0} MB", r.Tag, e.BytesReceived / 1048576.0, e.TotalBytesToReceive / 1048576.0));
                        var finished = new ManualResetEvent(false);
                        Exception failure = null;
                        web.DownloadFileCompleted += (s, e) => { failure = e.Error; finished.Set(); };
                        web.DownloadFileAsync(new Uri(r.Download), fresh);
                        finished.WaitOne();
                        if (failure != null) throw failure;
                    }
                    if (!LooksLikeProgram(fresh)) throw new InvalidDataException("the download isn't a Windows program");
                    progress(1, Lang.T("Installing..."));
                    if (File.Exists(old)) File.Delete(old);
                    File.Move(exe, old);
                    try { File.Move(fresh, exe); }
                    catch { File.Move(old, exe); throw; }   // put this copy back if the new one can't take its place
                    ready(delegate
                    {
                        Process.Start(exe, "--tray --after " + Process.GetCurrentProcess().Id);
                    });
                }
                catch (Exception ex)
                {
                    try { File.Delete(fresh); } catch { }
                    failed(Lang.F("Couldn't install the update: {0}", ex.Message));
                }
            });
        }

        static bool LooksLikeProgram(string path)
        {
            var info = new FileInfo(path);
            if (!info.Exists || info.Length < 64 * 1024) return false;
            using (FileStream f = File.OpenRead(path)) return f.ReadByte() == 'M' && f.ReadByte() == 'Z';
        }

        // At startup after an update: wait for the old copy to quit, then remove it.
        public static void AfterUpdate(string[] args)
        {
            int at = Array.IndexOf(args, "--after"), pid;
            if (at >= 0 && at + 1 < args.Length && int.TryParse(args[at + 1], out pid))
                try { Process.GetProcessById(pid).WaitForExit(15000); } catch { }
            try { File.Delete(Application.ExecutablePath + ".old"); } catch { }
        }

        static WebClient Client()
        {
            var web = new WebClient();
            web.Headers[HttpRequestHeader.UserAgent] = "BlinkBuddy";   // GitHub's API requires one
            return web;
        }
    }
}
