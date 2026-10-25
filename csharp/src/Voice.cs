using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;

namespace BlinkBuddy
{
    // Reads the guided break's lines aloud. Recorded clips (voice\<key>.wav or .mp3 next to BlinkBuddy.exe) are used
    // when present, played through Windows' media control interface so they can overlap the chime; otherwise a
    // Windows voice speaks the text.
    class Voice : IDisposable
    {
        [DllImport("winmm.dll", CharSet = CharSet.Unicode)]
        static extern int mciSendString(string command, StringBuilder result, int length, IntPtr callback);

        const string Alias = "blinkbuddyvoice";
        System.Speech.Synthesis.SpeechSynthesizer speaker;
        bool open;

        public static string Folder { get { return Path.Combine(Path.GetDirectoryName(Application.ExecutablePath), "voice"); } }

        public static bool HasClips
        {
            get { return Directory.Exists(Folder) && Directory.GetFiles(Folder, "done.*").Length > 0; }
        }

        // The look-away break's lines, in the order they are spoken.
        public static readonly string[] LookAwayKeys = { "far", "sides", "updown", "circle", "close", "done" };
        public const double Pause = 0.08;   // s between lines; the clips carry their own quiet at both ends too

        static readonly System.Collections.Generic.Dictionary<string, double> lengths = new System.Collections.Generic.Dictionary<string, double>();

        // How long a clip plays, in seconds (0 when there is none), measured once from the file itself.
        public static double Length(string key)
        {
            double s;
            if (lengths.TryGetValue(key, out s)) return s;
            s = 0;
            string clip = Clip(key);
            if (clip != null && mciSendString(string.Format("open \"{0}\" type mpegvideo alias blinkbuddymeasure", clip), null, 0, IntPtr.Zero) == 0)
            {
                var r = new StringBuilder(32);
                mciSendString("set blinkbuddymeasure time format milliseconds", null, 0, IntPtr.Zero);
                mciSendString("status blinkbuddymeasure length", r, 32, IntPtr.Zero);
                mciSendString("close blinkbuddymeasure", null, 0, IntPtr.Zero);
                int ms;
                if (int.TryParse(r.ToString(), out ms)) s = ms / 1000.0;
            }
            lengths[key] = s;
            return s;
        }

        // The shortest break, in whole seconds, that fits every line with a pause between them; 0 without clips.
        public static int MinimumSeconds(string[] keys)
        {
            double total = 0;
            foreach (string k in keys)
            {
                double l = Length(k);
                if (l <= 0) return 0;
                total += l;
            }
            return (int)Math.Ceiling(total + Pause * (keys.Length - 1));
        }

        public void Say(string key, string text)
        {
            Stop();
            string clip = Clip(key);
            if (clip != null)
            {
                if (mciSendString(string.Format("open \"{0}\" type mpegvideo alias {1}", clip, Alias), null, 0, IntPtr.Zero) == 0)
                {
                    open = true;
                    mciSendString("play " + Alias, null, 0, IntPtr.Zero);
                    return;
                }
            }
            try
            {
                if (speaker == null)
                {
                    speaker = new System.Speech.Synthesis.SpeechSynthesizer();
                    speaker.SetOutputToDefaultAudioDevice();
                    speaker.Rate = -1;
                }
                speaker.SpeakAsync(text);
            }
            catch { }   // no voices installed: the screen still shows the steps
        }

        static string Clip(string key)
        {
            foreach (string ext in new[] { ".wav", ".mp3" })
            {
                string path = Path.Combine(Folder, key + ext);
                if (File.Exists(path)) return path;
            }
            return null;
        }

        public void Stop()
        {
            if (open)
            {
                mciSendString("close " + Alias, null, 0, IntPtr.Zero);
                open = false;
            }
            if (speaker != null) try { speaker.SpeakAsyncCancelAll(); } catch { }
        }

        public void Dispose()
        {
            Stop();
            if (speaker != null) speaker.Dispose();
        }
    }
}
