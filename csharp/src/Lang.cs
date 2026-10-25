using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;

namespace BlinkBuddy
{
    // The interface language. Text is written in English in the code and looked up here as it reaches the screen:
    // lang\en.txt lists every translatable line, and lang\<code>.txt holds the same lines, in the same order, in that
    // language (an empty line, or one that's missing, stays English). "\n" in a line stands for a line break.
    static class Lang
    {
        public struct Info
        {
            public string Code, Name;
            public Info(string code, string name) { Code = code; Name = name; }
        }

        // Each in its own words.
        public static readonly Info[] All =
        {
            new Info("en", "English"), new Info("tr", "Türkçe"), new Info("de", "Deutsch"), new Info("fr", "Français"),
            new Info("es", "Español"), new Info("pt", "Português (Brasil)"), new Info("it", "Italiano"), new Info("nl", "Nederlands"),
            new Info("pl", "Polski"), new Info("cs", "Čeština"), new Info("hu", "Magyar"), new Info("ro", "Română"),
            new Info("sv", "Svenska"), new Info("da", "Dansk"), new Info("fi", "Suomi"), new Info("el", "Ελληνικά"),
            new Info("ru", "Русский"), new Info("uk", "Українська"), new Info("ja", "日本語"), new Info("ko", "한국어"),
            new Info("zh", "简体中文")
        };

        static Dictionary<string, string> table;
        public static string Code = "en";

        // "" picks the Windows display language when there is a translation for it, English otherwise.
        public static void Use(string code)
        {
            if (string.IsNullOrEmpty(code)) code = CultureInfo.CurrentUICulture.TwoLetterISOLanguageName;
            if (Array.FindIndex(All, i => i.Code == code) < 0) code = "en";
            Code = code;
            table = null;
            if (code == "en") return;
            string[] en = Read("en"), here = Read(code);
            if (en == null || here == null) return;
            table = new Dictionary<string, string>(en.Length);
            for (int i = 0; i < en.Length && i < here.Length; i++)
                if (en[i].Length > 0 && here[i].Length > 0) table[Unescape(en[i])] = Unescape(here[i]);
        }

        static string[] Read(string code)
        {
            using (Stream s = typeof(Lang).Assembly.GetManifestResourceStream("lang." + code))
            {
                if (s == null) return null;
                using (var r = new StreamReader(s, System.Text.Encoding.UTF8))
                    return r.ReadToEnd().Replace("\r", "").Split('\n');
            }
        }

        static string Unescape(string s) { return s.Replace("\\n", "\n").TrimEnd(); }

        // Diagnostics: with BLINKBUDDY_LANG_DUMP set to a file, every line asked for is noted there (once each), which
        // is how lang\en.txt is kept complete.
        static readonly string dump = Environment.GetEnvironmentVariable("BLINKBUDDY_LANG_DUMP");
        static readonly HashSet<string> seen = new HashSet<string>();

        public static string T(string english)
        {
            if (dump != null && !string.IsNullOrEmpty(english) && seen.Add(english))
                try { File.AppendAllText(dump, english.Replace("\n", "\\n") + "\n"); } catch { }
            string s;
            return english != null && table != null && table.TryGetValue(english, out s) ? s : english;
        }

        // A sentence with blanks: the English pattern ("Next break in {0} min") is looked up, then filled in.
        public static string F(string english, params object[] args) { return string.Format(T(english), args); }

        public static string[] T(string[] items)
        {
            var o = new string[items.Length];
            for (int i = 0; i < items.Length; i++) o[i] = T(items[i]);
            return o;
        }

        // For text drawn with GDI+ (the full-screen breaks), which doesn't borrow glyphs from other fonts.
        public static string Family
        {
            get { return Code == "ja" ? "Yu Gothic UI" : Code == "ko" ? "Malgun Gothic" : Code == "zh" ? "Microsoft YaHei UI" : "Segoe UI"; }
        }
    }
}
