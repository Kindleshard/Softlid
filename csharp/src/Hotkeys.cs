using System;
using System.Collections.Generic;
using System.Windows.Forms;

namespace BlinkBuddy
{
    // The keyboard shortcuts: each action, its default keys, and the keys you set instead (kept in Settings.Hotkeys as
    // "id=Ctrl+Alt+Shift+F;..."; an action set to nothing has no shortcut). The reading strip's movement keys only
    // work while the strip stays put (not following the mouse).
    static class Hotkeys
    {
        public const int Filter = 1, Brighter = 2, Dimmer = 3, Warmer = 4, Cooler = 5, Strip = 6, Awake = 7, NextMode = 8,
            BreakNow = 9, Mini = 10, StripUp = 11, StripDown = 12, StripLeft = 13, StripRight = 14, StripBigger = 15, StripSmaller = 16;
        public const int Count = 16;

        public static readonly string[] Names =
        {
            "Screen filter on or off", "Brighter", "Dimmer", "Warmer (more blue light filter)", "Cooler (less blue light filter)",
            "Reading strip on or off", "Keep PC awake on or off", "Next screen mode", "Take a break now", "Mini mode on or off",
            "Move the reading strip up", "Move the reading strip down", "Move the reading strip left", "Move the reading strip right",
            "Make the reading strip bigger", "Make the reading strip smaller"
        };

        static readonly string[] Defaults =
        {
            "Ctrl+Alt+Shift+F", "Ctrl+Alt+Shift+Up", "Ctrl+Alt+Shift+Down", "Ctrl+Alt+Shift+Right", "Ctrl+Alt+Shift+Left",
            "Ctrl+Alt+Shift+R", "Ctrl+Alt+Shift+K", "Ctrl+Alt+Shift+M", "Ctrl+Alt+Shift+B", "Ctrl+Alt+Shift+N",
            "Ctrl+Alt+Up", "Ctrl+Alt+Down", "Ctrl+Alt+Left", "Ctrl+Alt+Right", "Ctrl+Alt+Oemplus", "Ctrl+Alt+OemMinus"
        };

        // Held keys repeat for these; the switches fire once per press.
        public static bool Repeats(int id)
        {
            return id == Brighter || id == Dimmer || id == Warmer || id == Cooler || id >= StripUp;
        }

        public static bool IsStripMove(int id) { return id >= StripUp; }

        static Dictionary<int, string> Read(Settings s)
        {
            var d = new Dictionary<int, string>();
            foreach (string part in (s.Hotkeys ?? "").Split(';'))
            {
                int eq = part.IndexOf('='), id;
                if (eq > 0 && int.TryParse(part.Substring(0, eq), out id)) d[id] = part.Substring(eq + 1);
            }
            return d;
        }

        // The keys for an action ("" when it has none).
        public static string Get(Settings s, int id)
        {
            string v;
            return Read(s).TryGetValue(id, out v) ? v : Defaults[id - 1];
        }

        public static void Set(Settings s, int id, string keys)
        {
            Dictionary<int, string> d = Read(s);
            if (keys == Defaults[id - 1]) d.Remove(id);
            else d[id] = keys;
            var parts = new List<string>();
            foreach (KeyValuePair<int, string> kv in d) parts.Add(kv.Key + "=" + kv.Value);
            s.Hotkeys = string.Join(";", parts.ToArray());
        }

        public static void ResetAll(Settings s) { s.Hotkeys = ""; }

        // "Ctrl+Alt+Shift+F" into RegisterHotKey's modifiers and key.
        public static bool Parse(string text, out int mods, out Keys key)
        {
            mods = 0;
            key = Keys.None;
            if (string.IsNullOrEmpty(text)) return false;
            foreach (string part in text.Split('+'))
            {
                string p = part.Trim();
                if (p == "Ctrl") mods |= Native.MOD_CONTROL;
                else if (p == "Alt") mods |= Native.MOD_ALT;
                else if (p == "Shift") mods |= Native.MOD_SHIFT;
                else if (p == "Win") mods |= 0x8;
                else
                {
                    try { key = (Keys)Enum.Parse(typeof(Keys), p); } catch { return false; }
                }
            }
            return key != Keys.None && mods != 0;
        }

        // A key press as text: modifiers, then the key ("Ctrl+Alt+Shift+F"); "" while only modifiers are held.
        public static string Describe(Keys data)
        {
            Keys key = data & Keys.KeyCode;
            if (key == Keys.ControlKey || key == Keys.ShiftKey || key == Keys.Menu || key == Keys.LWin || key == Keys.RWin || key == Keys.None) return "";
            string t = "";
            if ((data & Keys.Control) != 0) t += "Ctrl+";
            if ((data & Keys.Alt) != 0) t += "Alt+";
            if ((data & Keys.Shift) != 0) t += "Shift+";
            return t.Length == 0 ? "" : t + key;
        }

        // For showing: arrows and the plus/minus keys as symbols.
        public static string Pretty(string keys)
        {
            if (string.IsNullOrEmpty(keys)) return "";
            return keys.Replace("+Up", "+↑").Replace("+Down", "+↓").Replace("+Left", "+←").Replace("+Right", "+→")
                .Replace("Oemplus", "=").Replace("OemMinus", "-").Replace("+", " + ");
        }
    }
}
