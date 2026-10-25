using System;
using System.IO;
using System.IO.Compression;
using System.Net;
using System.Text.RegularExpressions;
using System.Threading;

namespace BlinkBuddy
{
    // Optional set-up for the DesktopLUT engine: fetches the latest release of DesktopLUT (a separate, free program,
    // github.com/sup3rflyer/DesktopLUT) and unpacks its two files into BlinkBuddy's folder under your user profile.
    // Nothing runs here; the guard starts it later, with the usual permission prompt and 15-second check.
    static class LutSetup
    {
        const string Releases = "https://api.github.com/repos/sup3rflyer/DesktopLUT/releases/latest";
        public const string Page = "https://github.com/sup3rflyer/DesktopLUT";

        public static string Folder
        {
            get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "BlinkBuddy", "DesktopLUT"); }
        }

        // progress(text) and done(exePath or null, message) are called on a pool thread.
        public static void Install(Action<string> progress, Action<string, string> done)
        {
            ThreadPool.QueueUserWorkItem(delegate
            {
                string zip = Path.Combine(Path.GetTempPath(), "BlinkBuddy-DesktopLUT.zip");
                try
                {
                    ServicePointManager.SecurityProtocol |= SecurityProtocolType.Tls12;
                    string json;
                    progress("Looking up the latest DesktopLUT release...");
                    using (var web = Client()) json = web.DownloadString(Releases);
                    Match url = Regex.Match(json, "\"browser_download_url\"\\s*:\\s*\"([^\"]+\\.zip)\"");
                    Match tag = Regex.Match(json, "\"tag_name\"\\s*:\\s*\"([^\"]+)\"");
                    if (!url.Success)
                    {
                        done(null, "The latest release has no download BlinkBuddy recognises. Get it from " + Page + " and pick it with Browse.");
                        return;
                    }
                    string version = tag.Success ? tag.Groups[1].Value : "latest";
                    using (var web = Client())
                    {
                        web.DownloadProgressChanged += (s, e) =>
                            progress(string.Format("Downloading DesktopLUT {0}: {1:0.0} of {2:0.0} MB", version, e.BytesReceived / 1048576.0, e.TotalBytesToReceive / 1048576.0));
                        var finished = new ManualResetEvent(false);
                        Exception failure = null;
                        web.DownloadFileCompleted += (s, e) => { failure = e.Error; finished.Set(); };
                        web.DownloadFileAsync(new Uri(url.Groups[1].Value), zip);
                        finished.WaitOne();
                        if (failure != null) throw failure;
                    }

                    progress("Unpacking...");
                    Directory.CreateDirectory(Folder);
                    int found = 0;
                    using (ZipArchive archive = ZipFile.OpenRead(zip))
                        foreach (ZipArchiveEntry entry in archive.Entries)
                        {
                            if (entry.Name != "DesktopLUT.exe" && entry.Name != "DwmHook.dll") continue;   // only the program itself
                            entry.ExtractToFile(Path.Combine(Folder, entry.Name), true);
                            found++;
                        }
                    if (found < 2)
                    {
                        done(null, "The download didn't contain DesktopLUT.exe and DwmHook.dll. Get it from " + Page + " and pick it with Browse.");
                        return;
                    }
                    done(Path.Combine(Folder, "DesktopLUT.exe"), "DesktopLUT " + version + " is set up.");
                }
                catch (Exception ex)
                {
                    done(null, "Couldn't set it up: " + ex.Message);
                }
                finally
                {
                    try { File.Delete(zip); } catch { }
                }
            });
        }

        static WebClient Client()
        {
            var web = new WebClient();
            web.Headers[HttpRequestHeader.UserAgent] = "BlinkBuddy";   // GitHub's API requires one
            return web;
        }
    }
}
